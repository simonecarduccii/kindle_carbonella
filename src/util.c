/* Carbonella - small helpers: logging, memory, files, UTF-8, settings. */
#include "carbonella.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void log_msg(const char *fmt, ...)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    fprintf(stderr, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void out_of_memory(void)
{
    log_msg("out of memory");
    abort();
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        out_of_memory();
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p)
        out_of_memory();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        out_of_memory();
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s);
    char *d = xmalloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (n + 4096 + 1 > cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        n += r;
        if (r == 0)
            break;
    }
    fclose(f);
    buf[n] = 0;
    if (len)
        *len = n;
    return buf;
}

/* Writes to path.tmp and renames, so a crash never leaves a half-written file. */
int write_file_atomic(const char *path, const char *data, size_t len)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    int ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    fsync(fileno(f));
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int mkdir_p(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------ UTF-8 */

int utf8_decode(const unsigned char *s, size_t n, uint32_t *cp)
{
    if (n == 0) {
        *cp = 0;
        return 1;
    }
    unsigned c = s[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    int len;
    uint32_t v;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        v = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        v = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        v = c & 0x07;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    if ((size_t)len > n) {
        *cp = 0xFFFD;
        return 1;
    }
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        v = (v << 6) | (s[i] & 0x3F);
    }
    *cp = v;
    return len;
}

int utf8_encode(uint32_t cp, char *o)
{
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | (cp >> 6));
        o[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12));
        o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int utf8_valid(const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        int l = utf8_decode(u + i, n - i, &cp);
        if (cp == 0xFFFD && !(l == 3 && u[i] == 0xEF))
            return 0;
        i += l;
    }
    return 1;
}

char *latin1_to_utf8(const char *s, size_t n, size_t *out_len)
{
    char *o = xmalloc(n * 2 + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i++)
        j += utf8_encode((unsigned char)s[i], o + j);
    o[j] = 0;
    if (out_len)
        *out_len = j;
    return o;
}

/* --------------------------------------------------------------- settings */

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

static int settings_index(const Settings *s, const char *key)
{
    for (int i = 0; i < s->n; i++)
        if (strcmp(s->key[i], key) == 0)
            return i;
    return -1;
}

static void settings_put(Settings *s, const char *key, const char *val)
{
    int i = settings_index(s, key);
    if (i >= 0) {
        free(s->val[i]);
        s->val[i] = xstrdup(val);
        return;
    }
    if (s->n >= MAX_SETTINGS)
        return;
    s->key[s->n] = xstrdup(key);
    s->val[s->n] = xstrdup(val);
    s->n++;
}

void settings_load(Settings *s, const char *path)
{
    char *data = read_file(path, NULL);
    if (!data)
        return;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *l = trim(line);
        if (*l == '#' || *l == 0)
            continue;
        char *eq = strchr(l, '=');
        if (!eq)
            continue;
        *eq = 0;
        settings_put(s, trim(l), trim(eq + 1));
    }
    free(data);
}

const char *settings_get(const Settings *s, const char *key, const char *def)
{
    int i = settings_index(s, key);
    return i >= 0 ? s->val[i] : def;
}

int settings_int(const Settings *s, const char *key, int def)
{
    const char *v = settings_get(s, key, NULL);
    if (!v || !*v)
        return def;
    char *end;
    long n = strtol(v, &end, 10);
    return end == v ? def : (int)n;
}

void settings_set_int(Settings *s, const char *key, int v)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", v);
    settings_put(s, key, buf);
}

int settings_save(const Settings *s, const char *path, const char *header)
{
    size_t cap = 256, n = 0;
    char *buf = xmalloc(cap);
    buf[0] = 0;
    for (int i = -1; i < s->n; i++) {
        char line[512];
        int l = i < 0 ? snprintf(line, sizeof line, "%s\n", header ? header : "")
                      : snprintf(line, sizeof line, "%s = %s\n", s->key[i], s->val[i]);
        if (l < 0)
            continue;
        if ((size_t)l >= sizeof line)
            l = sizeof line - 1;
        if (n + l + 1 > cap) {
            cap = (n + l + 1) * 2;
            buf = xrealloc(buf, cap);
        }
        memcpy(buf + n, line, l);
        n += l;
    }
    int rc = write_file_atomic(path, buf, n);
    free(buf);
    return rc;
}
