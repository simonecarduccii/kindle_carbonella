/*
 * Carbonella - "Ricevi libri": books from the phone over Wi-Fi.
 *
 * While the screen is open, a tiny web server listens on the Kindle. The screen shows its address as a
 * QR code; the phone opens it and gets one simple page with a "choose the books" button. Each book is
 * sent with a plain HTTP PUT (the page does it with XMLHttpRequest, so it can show a progress bar) and
 * written to a hidden ".ricevo-*.part" file in the books folder, renamed to its real name only when it
 * is complete. Nothing else is served: no listing of the Kindle, no downloads, no deletes.
 *
 * Safety: the server exists only while the screen is open (rx_start .. rx_stop). The address has a
 * random token (http://IP:8080/k7f2): without it the server answers 403, so another phone on the same
 * network cannot guess it. Only .epub and .txt are accepted, names are cleaned (no folders, no ".."),
 * and a book never overwrites another one.
 *
 * The Kindle side: Wi-Fi is switched on if it was off (and off again at the end), and the port is
 * opened in the Kindle firewall (iptables) only while the server runs.
 *
 * Plain HTTP: the K4 cannot do modern TLS, and the books never leave the home network.
 * Everything is non-blocking: rx_step() does a bounded amount of work and returns at once, so the
 * screen keeps reading the keys.
 */
#include "carbonella.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define RX_MAX_CONN 4
#define RX_HDR_MAX 8192
#define RX_MAX_BOOK (300LL * 1024 * 1024) /* larger than any novel: protects the Kindle's space */
#define RX_STEP_BYTES (256 * 1024)          /* work per rx_step: keeps the screen responsive */
#define RX_CONN_TIMEOUT_MS 30000

typedef enum { C_FREE, C_HEADER, C_BODY, C_SEND } ConnState;

typedef struct {
    ConnState st;
    int fd;
    double last;     /* ms of the last byte in or out */
    char hdr[RX_HDR_MAX + 1];
    int hlen;
    /* upload */
    int out;         /* file being written, -1 none */
    long long len, got;
    int last_pct;
    char tmp[1024], final[1024], name[256];
    /* response */
    char *resp;
    size_t rlen, roff;
} Conn;

static struct {
    int on;
    int lfd;
    int port;
    char token[8];
    char books[512];
    char name[96]; /* device name, HTML-escaped, for the phone page */
    char *page;    /* PAGE with the name */
    char url[128], url_usb[128];
    char err[160];
    int wifi_turned_on, fw_open;
    double last_activity, last_ip_check;
    int idle_sent;
    Conn c[RX_MAX_CONN];
    RxEvent q[32];
    int qhead, qn;
    int sim_tick;
} R = { .lfd = -1 };

/* ------------------------------------------------------------- events */

static void push(int kind, const char *name, int percent, long long total)
{
    if (R.qn == (int)(sizeof R.q / sizeof R.q[0])) /* full: drop the oldest (only progress piles up) */
        R.qhead = (R.qhead + 1) % 32, R.qn--;
    RxEvent *e = &R.q[(R.qhead + R.qn) % 32];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->percent = percent;
    e->total = total;
    snprintf(e->name, sizeof e->name, "%s", name ? name : "");
    R.qn++;
}

static int pop(RxEvent *ev)
{
    if (R.qn == 0)
        return 0;
    *ev = R.q[R.qhead];
    R.qhead = (R.qhead + 1) % 32;
    R.qn--;
    return 1;
}

/* --------------------------------------------------------- the Kindle */

static int run(const char *cmd)
{
    int r = system(cmd);
    log_msg("ricevi: %s -> %d", cmd, r);
    return r;
}

/* Is the Wi-Fi radio on? With the framework stopped com.lab126.cmd wirelessEnable can say 1 while the
 * radio is off (18 nov test): the truth is whether wlan0 exists. -1 = not a Kindle (no lipc). */
static int wifi_enabled(void)
{
    if (access("/usr/bin/lipc-get-prop", X_OK) != 0)
        return -1;
    return access("/sys/class/net/wlan0", F_OK) == 0;
}

static void firewall(int open)
{
    char cmd[160];
    /* /usr/sbin is not in the PATH of KUAL scripts on the K4 */
    snprintf(cmd, sizeof cmd, "/usr/sbin/iptables %s INPUT -p tcp --dport %d -j ACCEPT 2>/dev/null", open ? "-I" : "-D",
             R.port);
    run(cmd);
}

/* IPv4 address of an interface ("wlan0", "usb0"), "" if it has none. */
static void if_addr(const char *ifname, char *out, size_t n)
{
    out[0] = 0;
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) != 0)
        return;
    for (struct ifaddrs *i = ifa; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !strcmp(i->ifa_name, ifname) &&
            (i->ifa_flags & IFF_UP)) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, n);
            break;
        }
    freeifaddrs(ifa);
}

int rx_wifi_state(void)
{
    if (g_sim)
        return 2;
    wifi_autoconnect_tick();
    char ip[64];
    if_addr("wlan0", ip, sizeof ip);
    if (ip[0])
        return 2;
    return wifi_enabled() == 1 ? 1 : 0;
}

static void check_addresses(void)
{
    char ip[64], url[128], usb[128] = "";
    if_addr("wlan0", ip, sizeof ip);
    if (ip[0])
        snprintf(url, sizeof url, "http://%s:%d/%s", ip, R.port, R.token);
    else
        url[0] = 0;
    if_addr("usb0", ip, sizeof ip);
    if (ip[0])
        snprintf(usb, sizeof usb, "http://%s:%d/%s", ip, R.port, R.token);
    if (strcmp(url, R.url) || strcmp(usb, R.url_usb)) {
        snprintf(R.url, sizeof R.url, "%s", url);
        snprintf(R.url_usb, sizeof R.url_usb, "%s", usb);
        log_msg("ricevi: address wifi '%s' usb '%s'", R.url, R.url_usb);
        push(RX_ADDRESS, R.url, 0, 0);
    }
}

/* ------------------------------------------------------------- the page */

/* One page, no external files, from the Figma frames "Ricevi libri · telefono" (texts word for word).
 * "@NOME@" becomes the device name ("Kindle di Simone"). Mono labels = IBM Plex Mono in Figma. */
static const char PAGE[] =
    "<!doctype html><html lang=\"it\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Carbonella</title><style>"
    "body{font:15px/1.45 -apple-system,system-ui,'IBM Plex Sans',sans-serif;margin:0 auto;padding:32px 24px;"
    "max-width:440px;color:#000;background:#fff}"
    ".m{font-family:'IBM Plex Mono',ui-monospace,Menlo,monospace}.k{font-size:11px;letter-spacing:.04em}"
    "h1{font-size:26px;line-height:1.25;font-weight:500;margin:20px 0 14px}p{margin:0 0 22px;color:#222}"
    ".b{display:block;width:100%;box-sizing:border-box;text-align:center;padding:15px;border-radius:6px;"
    "font-size:16px;font-weight:500;border:2px solid #000;background:#000;color:#fff;cursor:pointer}"
    ".b.o{background:#fff;color:#000;margin-top:22px}input{display:none}"
    "ul{list-style:none;padding:0;margin:6px 0 18px}li{padding:10px 0 8px;border-bottom:2px solid #000}"
    ".r{display:flex;justify-content:space-between;gap:12px}.n{font-weight:500;word-break:break-word}"
    ".s{font-size:13px;white-space:nowrap}.bar{height:10px;border:2px solid #000;margin-top:6px}"
    ".bar i{display:block;height:10px;width:0;background:#000}.e{font-size:14px;margin-top:8px}"
    "</style></head><body><div class=\"m k\">CARBONELLA</div>"
    "<h1>Manda libri al Kindle di @NOME@</h1>"
    "<div id=\"a\"><p>Scegli i libri sul telefono: arrivano sul Kindle e li trovi nella Home. "
    "Vanno bene i file .epub e .txt.</p><label class=\"b\" for=\"f\">Scegli i libri</label></div>"
    "<div id=\"w\" hidden><div class=\"m k\" id=\"t\">INVIO</div><ul id=\"l\"></ul>"
    "<p id=\"h\">Tieni aperta questa pagina finch\xc3\xa9 non hai finito.</p>"
    "<label class=\"b o\" for=\"f\" id=\"g\" hidden>Scegli altri libri</label></div>"
    "<input id=\"f\" type=\"file\" multiple><script>"
    "var q=[],busy=0,bad=0,$=function(i){return document.getElementById(i)},l=$('l');"
    "$('f').onchange=function(){if(!this.files.length)return;$('a').hidden=true;$('w').hidden=false;"
    "for(var i=0;i<this.files.length;i++)add(this.files[i]);this.value='';show();next()};"
    "function add(f){var li=document.createElement('li');li.innerHTML='<div class=r><span class=n></span>"
    "<span class=\"s m\">in coda</span></div><div class=bar><i></i></div>';li.querySelector('.n')."
    "textContent=f.name;l.appendChild(li);q.push([f,li])}"
    "function show(){var end=!busy&&!q.length;$('t').textContent=end&&!bad?'ARRIVATI SUL KINDLE':'INVIO';"
    "$('h').hidden=end;$('g').hidden=!end}"
    "function done(li,txt,err){li.querySelector('.s').textContent=txt;var b=li.querySelector('.bar');"
    "if(b)b.remove();if(err){bad=1;var e=document.createElement('div');e.className='e';e.textContent=err;"
    "li.appendChild(e)}busy=0;show();next()}"
    "function next(){if(busy||!q.length)return;busy=1;var t=q.shift(),f=t[0],li=t[1],"
    "s=li.querySelector('.s'),b=li.querySelector('.bar i');"
    "if(!/\\.(epub|txt)$/i.test(f.name)){done(li,'non va',"
    "'Il Kindle legge solo file .epub e .txt: questo non l\\'ho mandato.');return}"
    "s.textContent='0%';var x=new XMLHttpRequest();x.open('PUT',location.pathname.replace(/\\/$/,'')+"
    "'/libro?nome='+encodeURIComponent(f.name));x.upload.onprogress=function(e){if(e.lengthComputable){"
    "var p=Math.min(99,Math.round(e.loaded*100/e.total));b.style.width=p+'%';s.textContent=p+'%'}};"
    "x.onload=function(){var r={};try{r=JSON.parse(x.responseText)}catch(e){}"
    "if(x.status==200)done(li,'arrivato');else done(li,'non va',r.errore||('Errore '+x.status))};"
    "x.onerror=function(){done(li,'non va','Il Kindle non risponde: \xc3\xa8 ancora aperta la schermata "
    "Ricevi libri?')};x.send(f)}"
    "</script></body></html>";

static const char GONE[] =
    "<!doctype html><html lang=\"it\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Carbonella</title></head>"
    "<body style=\"font:17px/1.4 system-ui,sans-serif;padding:24px\"><h1>Indirizzo non valido</h1>"
    "<p>Apri \xe2\x80\x9cRicevi libri\xe2\x80\x9d sul Kindle e inquadra di nuovo il codice.</p></body></html>";

/* ------------------------------------------------------------ start/stop */

static void make_token(void)
{
    static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789"; /* no 0/o, 1/l/i: easy to type */
    unsigned char rnd[4] = { 0 };
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, rnd, sizeof rnd) != (ssize_t)sizeof rnd) {
        unsigned v = (unsigned)time(NULL) ^ (unsigned)getpid() * 2654435761u;
        memcpy(rnd, &v, 4);
    }
    if (fd >= 0)
        close(fd);
    for (int i = 0; i < 4; i++)
        R.token[i] = A[rnd[i] % (sizeof A - 1)];
    R.token[4] = 0;
}

static void make_page(const char *device)
{
    /* "Kindle di Simone" -> "Simone": the title already says "al Kindle di" */
    const char *who = device && *device ? device : "Carbonella";
    if (!strncasecmp(who, "Kindle di ", 10) && who[10])
        who += 10;
    size_t j = 0;
    for (const char *p = who; *p && j + 7 < sizeof R.name; p++) {
        const char *rep = *p == '<' ? "&lt;" : *p == '>' ? "&gt;" : *p == '&' ? "&amp;" : *p == '"' ? "&quot;" : NULL;
        if (rep) {
            memcpy(R.name + j, rep, strlen(rep));
            j += strlen(rep);
        } else
            R.name[j++] = *p;
    }
    R.name[j] = 0;
    const char *at = strstr(PAGE, "@NOME@");
    size_t n = sizeof PAGE + strlen(R.name);
    R.page = xmalloc(n);
    snprintf(R.page, n, "%.*s%s%s", (int)(at - PAGE), PAGE, R.name, at + 6);
}

int rx_start(const char *books_dir, const char *device_name)
{
    rx_stop();
    memset(&R, 0, sizeof R);
    R.lfd = -1;
    for (int i = 0; i < RX_MAX_CONN; i++)
        R.c[i].fd = R.c[i].out = -1;
    snprintf(R.books, sizeof R.books, "%s", books_dir);
    make_page(device_name);
    make_token();
    R.last_activity = now_ms();
    R.on = 1;

    if (g_sim) { /* fixed address and a fake book, for the screenshots of make prova */
        R.port = 8080;
        snprintf(R.url, sizeof R.url, "http://192.168.1.20:8080/abc123");
        push(RX_ADDRESS, R.url, 0, 0);
        return 0;
    }

    int en = wifi_enabled();
    if (en == 0) {
        wifi_radio_on();
        for (int i = 0; i < 20 && wifi_enabled() != 1; i++) /* wlan0 appears in 1-3 s */
            usleep(250000);
        if (wifi_enabled() == 1) {
            R.wifi_turned_on = 1;
        } else {
            /* with the USB cable (USBNetwork) the Mac can still send books: go on without Wi-Fi */
            char usb[64];
            if_addr("usb0", usb, sizeof usb);
            if (!usb[0]) {
                snprintf(R.err, sizeof R.err, "Il Wi-Fi è spento e non riesco ad accenderlo.");
                rx_stop();
                return -1;
            }
        }
    }

    R.lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (R.lfd < 0) {
        snprintf(R.err, sizeof R.err, "Non riesco ad aprire la connessione (%s).", strerror(errno));
        rx_stop();
        return -2;
    }
    int one = 1;
    setsockopt(R.lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY) };
    for (R.port = 8080; R.port < 8090; R.port++) {
        a.sin_port = htons(R.port);
        if (bind(R.lfd, (struct sockaddr *)&a, sizeof a) == 0)
            break;
    }
    if (R.port == 8090 || listen(R.lfd, 8) != 0) {
        snprintf(R.err, sizeof R.err, "Non riesco ad aprire la porta per il telefono (%s).", strerror(errno));
        rx_stop();
        return -3;
    }
    fcntl(R.lfd, F_SETFL, fcntl(R.lfd, F_GETFL) | O_NONBLOCK);
    firewall(1);
    R.fw_open = 1;
    check_addresses();
    R.last_ip_check = now_ms();
    log_msg("ricevi: server on port %d, token %s, wifi %s", R.port, R.token,
            R.wifi_turned_on ? "switched on" : "already on");
    return 0;
}

static void conn_close(Conn *c, const char *why)
{
    if (c->st == C_FREE)
        return;
    if (c->out >= 0) { /* a broken transfer: the partial file goes away */
        close(c->out);
        unlink(c->tmp);
        if (why)
            push(RX_FAILED, why, 0, 0);
        log_msg("ricevi: %s interrupted after %lld of %lld bytes", c->name, c->got, c->len);
    }
    if (c->fd >= 0)
        close(c->fd);
    free(c->resp);
    memset(c, 0, sizeof *c);
    c->fd = c->out = -1;
}

void rx_stop(void)
{
    if (!R.on && R.lfd < 0)
        return;
    for (int i = 0; i < RX_MAX_CONN; i++)
        conn_close(&R.c[i], NULL);
    if (R.lfd >= 0)
        close(R.lfd);
    R.lfd = -1;
    if (R.fw_open)
        firewall(0);
    R.fw_open = 0;
    if (R.wifi_turned_on) /* back as it was: Wi-Fi costs battery */
        wifi_radio_off();
    R.wifi_turned_on = 0;
    R.on = 0;
    R.url[0] = R.url_usb[0] = 0;
    R.qn = 0;
    free(R.page);
    R.page = NULL;
    log_msg("ricevi: server off");
}

const char *rx_error(void) { return R.err; }
const char *rx_url(void) { return R.url; }
const char *rx_url_usb(void) { return R.url_usb; }

/* ------------------------------------------------------------ requests */

static void respond(Conn *c, int code, const char *type, const char *body)
{
    const char *msg = code == 200 ? "OK" : code == 100 ? "Continue" : code == 403 ? "Forbidden" :
                      code == 404 ? "Not Found" : code == 413 ? "Payload Too Large" :
                      code == 415 ? "Unsupported Media Type" : code == 507 ? "Insufficient Storage" : "Bad Request";
    size_t bl = strlen(body);
    free(c->resp);
    c->resp = xmalloc(bl + 256);
    c->rlen = (size_t)snprintf(c->resp, bl + 256,
                               "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                               "Cache-Control: no-store\r\nConnection: close\r\n\r\n%s",
                               code, msg, type, bl, body);
    c->roff = 0;
    c->st = C_SEND;
}

static void respond_json_error(Conn *c, int code, const char *text)
{
    char body[400];
    snprintf(body, sizeof body, "{\"errore\":\"%s\"}", text); /* our own texts: no quotes inside */
    respond(c, code, "application/json; charset=utf-8", body);
}

static int hexv(int ch)
{
    return isdigit(ch) ? ch - '0' : isxdigit(ch) ? (tolower(ch) - 'a' + 10) : -1;
}

/* "Le%20citt%C3%A0.epub" -> "Le città.epub" */
static void url_decode_into(const char *s, size_t n, char *out, size_t outn)
{
    size_t j = 0;
    for (size_t i = 0; i < n && j + 1 < outn; i++) {
        if (s[i] == '%' && i + 2 < n && hexv((unsigned char)s[i + 1]) >= 0 && hexv((unsigned char)s[i + 2]) >= 0) {
            out[j++] = (char)(hexv((unsigned char)s[i + 1]) * 16 + hexv((unsigned char)s[i + 2]));
            i += 2;
        } else
            out[j++] = s[i] == '+' ? ' ' : s[i];
    }
    out[j] = 0;
}

/* A safe file name: no folders, no characters the FAT disk refuses, no leading dots, at most 180 bytes
 * (cut on a UTF-8 boundary). Returns 0 if nothing usable is left. */
static int clean_file_name(char *s)
{
    char *base = s;
    for (char *p = s; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    memmove(s, base, strlen(base) + 1);
    for (unsigned char *p = (unsigned char *)s; *p; p++)
        if (*p < 32 || strchr(":*?\"<>|", *p))
            *p = '-';
    while (*s == '.' || *s == ' ')
        memmove(s, s + 1, strlen(s));
    size_t n = strlen(s);
    if (n > 180) {
        size_t k = 180;
        while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80)
            k--;
        const char *ext = strrchr(s, '.');
        char e[8] = "";
        if (ext && strlen(ext) < sizeof e)
            snprintf(e, sizeof e, "%s", ext);
        s[k - strlen(e) > 0 ? k - strlen(e) : 0] = 0;
        strcat(s, e);
    }
    if (!utf8_valid(s, strlen(s))) /* a broken name: keep only ASCII */
        for (unsigned char *p = (unsigned char *)s; *p; p++)
            if (*p >= 128)
                *p = '_';
    return s[0] != 0;
}

static int has_ext(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a > b && !strcasecmp(s + a - b, ext);
}

static const char *header(const char *h, const char *name, char *out, size_t n)
{
    size_t k = strlen(name);
    for (const char *p = strstr(h, "\r\n"); p && p[2]; p = strstr(p + 2, "\r\n")) {
        const char *line = p + 2;
        if (!strncasecmp(line, name, k) && line[k] == ':') {
            const char *v = line + k + 1;
            while (*v == ' ')
                v++;
            size_t l = strcspn(v, "\r\n");
            snprintf(out, n, "%.*s", (int)(l < n ? l : n - 1), v);
            return out;
        }
    }
    return NULL;
}

static void start_upload(Conn *c, const char *query, long long len, int expect_continue)
{
    const char *q = strstr(query, "nome=");
    char name[600] = "";
    if (q) {
        q += 5;
        url_decode_into(q, strcspn(q, "&"), name, sizeof name);
    }
    if (!clean_file_name(name)) {
        respond_json_error(c, 400, "Nome del file non valido.");
        return;
    }
    if (!has_ext(name, ".epub") && !has_ext(name, ".txt")) {
        respond_json_error(c, 415, "Carbonella legge solo libri EPUB e TXT.");
        push(RX_FAILED, "Arrivato un file che non è un EPUB o un TXT.", 0, 0);
        return;
    }
    if (len <= 0) {
        respond_json_error(c, 400, "Il file è vuoto.");
        return;
    }
    if (len > RX_MAX_BOOK) {
        respond_json_error(c, 413, "Il file è troppo grande per un libro.");
        return;
    }
    struct statvfs vs;
    if (statvfs(R.books, &vs) == 0 && (long long)vs.f_bavail * (long long)vs.f_frsize < len + 2 * 1024 * 1024) {
        respond_json_error(c, 507, "Sul Kindle non c'è più spazio.");
        push(RX_FAILED, "Sul Kindle non c'è più spazio.", 0, 0);
        return;
    }
    /* never overwrite: "Libro.epub", "Libro (2).epub", ... */
    const char *dot = strrchr(name, '.');
    int stem = (int)(dot - name);
    snprintf(c->final, sizeof c->final, "%s/%s", R.books, name);
    for (int k = 2; access(c->final, F_OK) == 0 && k < 100; k++)
        snprintf(c->final, sizeof c->final, "%s/%.*s (%d)%s", R.books, stem, name, k, dot);
    const char *base = strrchr(c->final, '/');
    snprintf(c->name, sizeof c->name, "%.200s", base ? base + 1 : c->final);
    static unsigned seq;
    snprintf(c->tmp, sizeof c->tmp, "%s/.ricevo-%u-%u.part", R.books, (unsigned)getpid(), ++seq);
    c->out = open(c->tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (c->out < 0) {
        respond_json_error(c, 500, "Non riesco a scrivere sul Kindle.");
        push(RX_FAILED, "Non riesco a scrivere il libro sul Kindle.", 0, 0);
        return;
    }
    c->len = len;
    c->got = 0;
    c->last_pct = -1;
    c->st = C_BODY;
    if (expect_continue) {
        static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
        if (write(c->fd, cont, sizeof cont - 1) < 0)
            log_msg("ricevi: 100-continue not sent");
    }
    log_msg("ricevi: receiving %s (%lld bytes)", c->name, len);
    push(RX_STARTED, c->name, 0, len);
}

static void finish_upload(Conn *c)
{
    int ok = fsync(c->out) == 0;
    ok &= close(c->out) == 0;
    c->out = -1;
    if (!ok || rename(c->tmp, c->final) != 0) {
        unlink(c->tmp);
        respond_json_error(c, 500, "Non riesco a salvare il libro sul Kindle.");
        push(RX_FAILED, "Non riesco a salvare il libro sul Kindle.", 0, 0);
        return;
    }
    log_msg("ricevi: saved %s", c->final);
    char body[400];
    snprintf(body, sizeof body, "{\"ok\":true}");
    respond(c, 200, "application/json; charset=utf-8", body);
    push(RX_DONE, c->name, 100, c->len);
}

/* A complete header is in c->hdr: route it. */
static void handle_request(Conn *c, char *body_start, int body_bytes)
{
    char method[8] = "", target[1024] = "";
    sscanf(c->hdr, "%7s %1023s", method, target);
    char *query = strchr(target, '?');
    if (query)
        *query++ = 0;
    else
        query = (char *)"";
    char path_tok[16];
    snprintf(path_tok, sizeof path_tok, "/%s", R.token);
    size_t tl = strlen(path_tok);
    int tok_ok = !strncmp(target, path_tok, tl) && (target[tl] == 0 || target[tl] == '/');
    const char *rest = tok_ok ? target + tl : "";

    if (!tok_ok) {
        respond(c, strcmp(target, "/favicon.ico") ? 403 : 404, "text/html; charset=utf-8", GONE);
        return;
    }
    if (!strcmp(method, "GET") && (!*rest || !strcmp(rest, "/"))) {
        respond(c, 200, "text/html; charset=utf-8", R.page);
        return;
    }
    if ((!strcmp(method, "PUT") || !strcmp(method, "POST")) && !strcmp(rest, "/libro")) {
        char v[64];
        long long len = header(c->hdr, "Content-Length", v, sizeof v) ? atoll(v) : -1;
        int cont = header(c->hdr, "Expect", v, sizeof v) && !strcasecmp(v, "100-continue");
        start_upload(c, query, len, cont);
        if (c->st == C_BODY && body_bytes > 0) { /* body bytes that came with the header */
            long long take = body_bytes < c->len ? body_bytes : c->len;
            if (write(c->out, body_start, (size_t)take) != take) {
                conn_close(c, "Non riesco a scrivere il libro sul Kindle.");
                return;
            }
            c->got = take;
            if (c->got == c->len)
                finish_upload(c);
        }
        return;
    }
    respond(c, 404, "text/plain; charset=utf-8", "Non trovato\n");
}

/* Reads what is there for one connection, at most budget bytes. Returns the bytes read. */
static int conn_read(Conn *c, int budget)
{
    static char buf[64 * 1024];
    int total = 0;
    while (total < budget && (c->st == C_HEADER || c->st == C_BODY)) {
        size_t want = sizeof buf;
        if (c->st == C_HEADER)
            want = (size_t)(RX_HDR_MAX - c->hlen);
        else if ((long long)want > c->len - c->got)
            want = (size_t)(c->len - c->got);
        if (want == 0) { /* header too long */
            respond(c, 400, "text/plain", "Richiesta troppo lunga\n");
            break;
        }
        ssize_t r = c->st == C_HEADER ? read(c->fd, c->hdr + c->hlen, want) : read(c->fd, buf, want);
        if (r == 0) {
            conn_close(c, c->st == C_BODY ? "Il telefono ha interrotto l'invio." : NULL);
            return total;
        }
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                break;
            conn_close(c, "La connessione con il telefono si è interrotta.");
            return total;
        }
        total += (int)r;
        c->last = R.last_activity = now_ms();
        if (c->st == C_HEADER) {
            c->hlen += (int)r;
            c->hdr[c->hlen] = 0;
            char *end = strstr(c->hdr, "\r\n\r\n");
            if (end) {
                end[2] = 0; /* header lines end with \r\n, the blank line is cut */
                handle_request(c, end + 4, c->hlen - (int)(end + 4 - c->hdr));
            }
        } else {
            if (write(c->out, buf, (size_t)r) != r) {
                respond_json_error(c, 507, "Sul Kindle non c'è più spazio.");
                close(c->out);
                unlink(c->tmp);
                c->out = -1;
                push(RX_FAILED, "Non riesco a scrivere il libro sul Kindle.", 0, 0);
                break;
            }
            c->got += r;
            int pct = (int)(c->got * 100 / c->len);
            if (pct >= c->last_pct + 5 && pct < 100) {
                c->last_pct = pct;
                push(RX_PROGRESS, c->name, pct, c->len);
            }
            if (c->got == c->len)
                finish_upload(c);
        }
    }
    return total;
}

static void conn_write(Conn *c)
{
    while (c->roff < c->rlen) {
        ssize_t w = write(c->fd, c->resp + c->roff, c->rlen - c->roff);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return;
            conn_close(c, NULL);
            return;
        }
        c->roff += (size_t)w;
        c->last = now_ms();
    }
    shutdown(c->fd, SHUT_WR);
    conn_close(c, NULL);
}

/* Simulator: STARTED, PROGRESS 50, DONE for "Il barone rampante.epub", a few calls apart. */
static void sim_step(void)
{
    R.sim_tick++;
    if (R.sim_tick == 4)
        push(RX_STARTED, "Il barone rampante.epub", 0, 412345);
    else if (R.sim_tick == 6)
        push(RX_PROGRESS, "Il barone rampante.epub", 50, 412345);
    else if (R.sim_tick == 8)
        push(RX_DONE, "Il barone rampante.epub", 100, 412345);
}

int rx_step(RxEvent *ev)
{
    memset(ev, 0, sizeof *ev);
    if (!R.on)
        return 0;
    if (g_sim) {
        sim_step();
        return pop(ev);
    }
    double now = now_ms();
    wifi_autoconnect_tick();
    if (now - R.last_ip_check > 1000) { /* Wi-Fi may still be connecting, or the network may change */
        R.last_ip_check = now;
        check_addresses();
    }

    struct pollfd p[RX_MAX_CONN + 1];
    int np = 0, idx[RX_MAX_CONN];
    p[np++] = (struct pollfd){ .fd = R.lfd, .events = POLLIN };
    for (int i = 0; i < RX_MAX_CONN; i++)
        if (R.c[i].st != C_FREE) {
            idx[np - 1] = i;
            p[np++] = (struct pollfd){ .fd = R.c[i].fd, .events = R.c[i].st == C_SEND ? POLLOUT : POLLIN };
        }
    if (poll(p, (nfds_t)np, 0) > 0) {
        if (p[0].revents & POLLIN) {
            int fd = accept(R.lfd, NULL, NULL);
            if (fd >= 0) {
                Conn *c = NULL;
                for (int i = 0; i < RX_MAX_CONN && !c; i++)
                    if (R.c[i].st == C_FREE)
                        c = &R.c[i];
                if (!c) {
                    close(fd); /* busy: the page retries the next book later */
                } else {
                    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
                    memset(c, 0, sizeof *c);
                    c->fd = fd;
                    c->out = -1;
                    c->st = C_HEADER;
                    c->last = R.last_activity = now;
                    R.idle_sent = 0;
                }
            }
        }
        int budget = RX_STEP_BYTES;
        for (int k = 1; k < np; k++) {
            Conn *c = &R.c[idx[k - 1]];
            if (c->st == C_FREE || !p[k].revents)
                continue;
            if (c->st == C_SEND)
                conn_write(c);
            else if (budget > 0)
                budget -= conn_read(c, budget);
            if (c->st == C_SEND) /* answer right away if we can */
                conn_write(c);
        }
    }
    for (int i = 0; i < RX_MAX_CONN; i++)
        if (R.c[i].st != C_FREE && now - R.c[i].last > RX_CONN_TIMEOUT_MS)
            conn_close(&R.c[i], "Il telefono ha smesso di mandare il libro.");
    if (!R.idle_sent && now - R.last_activity > RX_IDLE_MIN * 60000.0) {
        R.idle_sent = 1;
        push(RX_IDLE, "", 0, 0);
    }
    return pop(ev);
}
#ifdef RX_TEST
const char *rx_token_for_test(void) { return R.token; }
#endif
