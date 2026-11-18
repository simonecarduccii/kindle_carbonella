/*
 * Carbonella - choosing a Wi-Fi network without the Amazon interface (for "Ricevi libri").
 *
 * With Carbonella open the Kindle "framework" is stopped, but wifid (the Wi-Fi service) still answers
 * on lipc. What we know from the tests on Simone's K4 (18 nov 2026, diario/2026-11-18-wifi-scegli-rete.md):
 *   - com.lab126.cmd wirelessEnable = 1 alone does NOT switch the radio on with the framework stopped
 *     (wifid "711" says Wireless: Off, no wlan0): we also set com.lab126.wifid enable = 1;
 *   - scan:        lipc-set-prop com.lab126.wifid scan ""
 *   - networks:    lipc-hash-prop -n com.lab126.wifid scanList
 *                  { essid = "TIM-1", secured = "yes", known = "no", signal = (1), signal_max = (5), ... } {...}
 *   - new network: lipc-hash-prop com.lab126.wifid createProfile, a hasharray on stdin
 *                  { essid = "Casa", secured = "yes", psk = "password", store_nw_user_pref = (0) }
 *                  (the same keys KOReader uses on Kindles), then cmConnect "Casa";
 *   - state:       lipc-get-prop com.lab126.wifid cmState (CONNECTED when done) and wlan0's address.
 * wifid remembers the networks (profiles): the next time the Kindle connects by itself.
 */
#include "carbonella.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define WIFI_TIMEOUT_MS 45000 /* DHCP on a phone hotspot can take 20-30 s */

static struct {
    char essid[64];
    int new_profile;
    double t0;
    int active;
} W;

static int cmd_out(const char *cmd, char *out, size_t n)
{
    out[0] = 0;
    FILE *f = popen(cmd, "r");
    if (!f)
        return -1;
    size_t len = fread(out, 1, n - 1, f);
    out[len] = 0;
    return pclose(f);
}

static void quote(const char *s, char *out, size_t n) /* for a shell argument in '...' */
{
    size_t j = 0;
    out[j++] = '\'';
    for (; *s && j + 6 < n; s++) {
        if (*s == '\'') {
            memcpy(out + j, "'\\''", 4);
            j += 4;
        } else
            out[j++] = *s;
    }
    out[j++] = '\'';
    out[j] = 0;
}

int wifi_radio_on(void)
{
    if (g_sim)
        return 0;
    int r = system("lipc-set-prop com.lab126.cmd wirelessEnable 1 2>/dev/null");
    r |= system("lipc-set-prop -i com.lab126.wifid enable 1 2>/dev/null");
    log_msg("wifi: radio on (%d)", r);
    return r == 0 ? 0 : -1;
}

void wifi_scan(void)
{
    if (!g_sim)
        (void)!system("lipc-set-prop com.lab126.wifid scan '' 2>/dev/null");
}

/* Reads a value after "key =" inside one { } block: "string" (with \" escapes) or (int). */
static int hval(const char *blk, const char *end, const char *key, char *out, size_t n)
{
    size_t k = strlen(key);
    for (const char *p = blk; p + k < end; p++) {
        if (strncmp(p, key, k) || (p > blk && (isalnum((unsigned char)p[-1]) || p[-1] == '_')))
            continue;
        const char *q = p + k;
        while (q < end && *q == ' ')
            q++;
        if (q >= end || *q != '=')
            continue;
        q++;
        while (q < end && *q == ' ')
            q++;
        size_t j = 0;
        if (*q == '"') {
            for (q++; q < end && *q != '"' && j + 1 < n; q++) {
                if (*q == '\\' && q + 1 < end)
                    q++;
                out[j++] = *q;
            }
        } else if (*q == '(') {
            for (q++; q < end && *q != ')' && j + 1 < n; q++)
                out[j++] = *q;
        } else
            return 0;
        out[j] = 0;
        return 1;
    }
    return 0;
}

static int cmp_net(const void *a, const void *b)
{
    const WifiNet *x = a, *y = b;
    if (x->known != y->known)
        return y->known - x->known;
    if (x->signal != y->signal)
        return y->signal - x->signal;
    return strcasecmp(x->essid, y->essid);
}

/* Parses lipc-hash-prop output (exposed for the tests). */
int wifi_parse_scan(const char *s, WifiNet *out, int max)
{
    int n = 0;
    for (const char *p = strchr(s, '{'); p && n < max; p = strchr(p, '{')) {
        const char *end = strchr(p, '}');
        if (!end)
            break;
        WifiNet w = { 0 };
        char v[96];
        if (hval(p, end, "essid", w.essid, sizeof w.essid) && w.essid[0]) {
            w.secured = !(hval(p, end, "secured", v, sizeof v) && !strcmp(v, "no"));
            w.known = hval(p, end, "known", v, sizeof v) && !strcmp(v, "yes");
            int sig = hval(p, end, "signal", v, sizeof v) ? atoi(v) : 0;
            int max_sig = hval(p, end, "signal_max", v, sizeof v) ? atoi(v) : 5;
            if (max_sig <= 0)
                max_sig = 5;
            w.signal = sig * 100 / max_sig;
            w.bars = w.signal >= 70 ? 3 : w.signal >= 35 ? 2 : 1; /* Forte / Medio / Debole */
            int dup = 0;
            for (int i = 0; i < n; i++)
                if (!strcmp(out[i].essid, w.essid)) { /* same name twice (2.4 GHz + mesh): keep the best */
                    dup = 1;
                    if (w.signal > out[i].signal)
                        out[i] = w;
                }
            if (!dup)
                out[n++] = w;
        }
        p = end + 1;
    }
    qsort(out, n, sizeof *out, cmp_net);
    return n;
}

int wifi_list(WifiNet *out, int max)
{
    if (g_sim) { /* the networks of the Figma frame */
        static const char demo[] = "{ essid = \"Casa\", secured = \"yes\", known = \"no\", signal = (5), signal_max = (5) }"
                                   "{ essid = \"Casa_5G\", secured = \"yes\", known = \"no\", signal = (3), signal_max = (5) }"
                                   "{ essid = \"Ospiti\", secured = \"no\", known = \"no\", signal = (3), signal_max = (5) }"
                                   "{ essid = \"TIM-38492011\", secured = \"yes\", known = \"no\", signal = (1), signal_max = (5) }";
        return wifi_parse_scan(demo, out, max);
    }
    static char buf[32768];
    cmd_out("lipc-hash-prop -n com.lab126.wifid scanList 2>/dev/null", buf, sizeof buf);
    int n = wifi_parse_scan(buf, out, max);
    log_msg("wifi: %d networks", n);
    return n;
}

static int has_address(void)
{
    char ip[64] = "";
    FILE *f = popen("ifconfig wlan0 2>/dev/null", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char *a = strstr(line, "inet addr:");
            if (a && sscanf(a + 10, "%63s", ip) == 1)
                break;
        }
        pclose(f);
    }
    return ip[0] != 0;
}

int wifi_connect(const char *essid, const char *password)
{
    memset(&W, 0, sizeof W);
    snprintf(W.essid, sizeof W.essid, "%s", essid);
    W.t0 = now_ms();
    W.active = 1;
    if (g_sim)
        return 0;
    char q[200];
    quote(essid, q, sizeof q);
    if (password) { /* a new network (or a new password): wifid saves it as a profile */
        W.new_profile = 1;
        char tmp[] = "/tmp/carbonella-wifi-XXXXXX";
        int fd = mkstemp(tmp); /* 0600, in RAM, deleted right after: the password never stays on disk */
        if (fd < 0)
            return -1;
        FILE *f = fdopen(fd, "w");
        fprintf(f, "{ essid = \"");
        for (const char *p = essid; *p; p++)
            fprintf(f, *p == '"' || *p == '\\' ? "\\%c" : "%c", *p);
        if (*password) {
            fprintf(f, "\", secured = \"yes\", psk = \"");
            for (const char *p = password; *p; p++)
                fprintf(f, *p == '"' || *p == '\\' ? "\\%c" : "%c", *p);
            fprintf(f, "\", store_nw_user_pref = (0) }\n");
        } else
            fprintf(f, "\", secured = \"no\", store_nw_user_pref = (0) }\n");
        fclose(f);
        char cmd[256], out[512];
        snprintf(cmd, sizeof cmd, "lipc-hash-prop com.lab126.wifid createProfile < %s 2>&1", tmp);
        int r = cmd_out(cmd, out, sizeof out);
        unlink(tmp);
        log_msg("wifi: profile for %s -> %d", essid, r); /* never log the password or the output */
        if (r != 0)
            return -1;
    }
    char cmd[300];
    snprintf(cmd, sizeof cmd, "lipc-set-prop com.lab126.wifid cmConnect %s 2>/dev/null", q);
    int r = system(cmd);
    log_msg("wifi: connect %s -> %d", essid, r);
    return r == 0 ? 0 : -1;
}

int wifi_connect_poll(void)
{
    if (!W.active)
        return -2;
    double t = now_ms() - W.t0;
    if (g_sim) {
        if (t < 1500)
            return 0;
        W.active = 0;
        return strcmp(W.essid, "TIM-38492011") ? 1 : -1; /* the last one shows "Password sbagliata" */
    }
    char st[64];
    cmd_out("lipc-get-prop com.lab126.wifid cmState 2>/dev/null", st, sizeof st);
    st[strcspn(st, "\r\n")] = 0;
    if ((!strcasecmp(st, "CONNECTED") || t > 3000) && has_address()) {
        log_msg("wifi: connected to %s in %.0f ms", W.essid, t);
        W.active = 0;
        return 1;
    }
    if (t > WIFI_TIMEOUT_MS) {
        log_msg("wifi: no connection to %s after %.0f ms (cmState %s)", W.essid, t, st);
        W.active = 0;
        if (W.new_profile) { /* with a password just typed, the usual reason is a wrong password: */
            char q[200], cmd[300]; /* forget it, so the Kindle does not keep trying with it */
            quote(W.essid, q, sizeof q);
            snprintf(cmd, sizeof cmd, "lipc-set-prop com.lab126.wifid deleteProfile %s 2>/dev/null", q);
            (void)!system(cmd);
            return -1;
        }
        return -2;
    }
    return 0;
}

void wifi_connect_cancel(void)
{
    W.active = 0;
}
