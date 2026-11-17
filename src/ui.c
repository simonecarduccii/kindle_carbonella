/*
 * Carbonella - pieces of the interface drawn as in the Figma design
 * (https://www.figma.com/design/TD5USYE5DwkvWALoR821lL): the top bar, menu rows, the reading
 * progress footer and the 23 generated covers.
 *
 * All measures come from the Figma frames (600x800, the same pixels as the Kindle 4 screen).
 * Covers are designed at 160x232 and scaled: s = width / 160.
 */
#include "carbonella.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int g_sim; /* simulator: fixed clock and battery, so screenshots can be compared with Figma */

/* Baseline of a line of text whose line box starts at 'top' and is 'lh' tall, as CSS does it. */
int ui_baseline(Face f, int size, float top, float lh)
{
    int a = font_ascent(f, size), d = font_descent(f, size);
    return (int)lrintf(top + (lh - (a + d)) / 2 + a);
}

static int battery_percent(void)
{
    if (g_sim)
        return 100; /* as in the Figma frames */
    char *s = read_file("/sys/devices/system/yoshi_battery/yoshi_battery0/battery_capacity", NULL);
    int v = s ? atoi(s) : -1;
    free(s);
    return v;
}

void ui_clock(char *out, size_t n)
{
    if (g_sim) {
        snprintf(out, n, "21:40");
        return;
    }
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* "Header" component: 48 px, Plex Mono 16, name or title on the left, time and battery on the right,
 * an optional small text in the middle and an optional progress bar on the bottom edge. */
void ui_header(Canvas *c, const char *left, const char *center, double bar)
{
    const int W = c->w;
    canvas_fill(c, 0, 0, W, 48, G_CARTA);
    canvas_fill(c, 0, 47, W, 1, G_CENERE);
    int base = ui_baseline(FACE_MONO, 16, 6, 36);

    /* right group, from the right edge: pole, battery, percentage, time */
    int x = W - 32;
    int pct = battery_percent();
    /* battery icon (Simone, v0.6.2): 24 x 13 body, 2 px outline with rounded corners, 18 x 7 charge
     * 1 px inside, 2 x 6 pole */
    canvas_fill(c, x - 2, 20, 2, 6, G_NERO);
    x -= 2 + 24;
    canvas_frame(c, x, 17, 24, 13, 2, G_NERO);
    canvas_fill(c, x, 17, 1, 1, G_CARTA); /* radius 1 */
    canvas_fill(c, x + 23, 17, 1, 1, G_CARTA);
    canvas_fill(c, x, 29, 1, 1, G_CARTA);
    canvas_fill(c, x + 23, 29, 1, 1, G_CARTA);
    if (pct > 0) {
        int fw = (int)lrintf(18.0f * (pct > 100 ? 100 : pct) / 100);
        canvas_fill(c, x + 3, 20, fw < 1 ? 1 : fw, 7, G_NERO);
    }
    char buf[16];
    if (pct >= 0) {
        snprintf(buf, sizeof buf, "%d%%", pct);
        x -= 6 + font_text_width(FACE_MONO, 16, buf);
        font_draw_text(c, FACE_MONO, 16, x, base, buf, G_GRAFITE, 0);
    }
    ui_clock(buf, sizeof buf);
    x -= 16 + font_text_width(FACE_MONO, 16, buf);
    font_draw_text(c, FACE_MONO, 16, x, base, buf, G_NERO, 0);
    int right_start = x;

    int left_w = center ? 116 : right_start - 32 - 16;
    if (left)
        font_draw_text(c, FACE_MONO, 16, 32, base, left, G_NERO, center ? 165 : left_w);
    if (center) {
        /* 102 px box, space shared equally between the three groups, text at its bottom */
        float cx = 32 + 116 + (right_start - 32 - 116 - 102) / 2.0f + 51;
        int w = font_text_width(FACE_MONO, 10, center);
        font_draw_text(c, FACE_MONO, 10, (int)lrintf(cx - w / 2.0f), ui_baseline(FACE_MONO, 10, 26.5f, 12), center,
                       G_NERO, 0);
    }
    if (bar >= 0) {
        canvas_fill(c, 0, 47, W, 2, G_VELO);
        canvas_fill(c, 0, 47, (int)lrint(W * (bar > 1 ? 1 : bar)), 2, G_CENERE);
    }
}

/* Reading footer: "12 di 684" (or "2%" on the left and "pagina 12 di 684" on the right) and a
 * 3 px bar of the whole book. Bottom-anchored at y_bottom. */
void ui_progress(Canvas *c, int x, int w, int y_bottom, const char *left, const char *center, const char *right,
                 double frac, uint8_t track)
{
    int bar_y = y_bottom - 9 - 3;
    float text_top = bar_y - 10 - 16.8f;
    int base = ui_baseline(FACE_MONO, 14, text_top, 16.8f);
    if (center) {
        int tw = font_text_width(FACE_MONO, 14, center);
        font_draw_text(c, FACE_MONO, 14, x + (w - tw) / 2, base, center, G_GRAFITE, 0); /* #222 since v0.6 */
    }
    if (left)
        font_draw_text(c, FACE_MONO, 14, x, base, left, G_GRAFITE, 0);
    if (right) {
        int tw = font_text_width(FACE_MONO, 14, right);
        font_draw_text(c, FACE_MONO, 14, x + w - tw, base, right, G_NERO, 0);
    }
    canvas_fill(c, x, bar_y, w, 3, track);
    int done = (int)lrint(w * (frac < 0 ? 0 : frac > 1 ? 1 : frac));
    if (done < 2 && frac > 0)
        done = 2;
    canvas_fill(c, x, bar_y, done, 3, G_NERO); /* black, as in Figma since 17 nov */
}

/* "›" of the menu rows: 4.44 x 8.48 */
static void chevron(Canvas *c, float x, float y, uint8_t ink)
{
    float xy[6] = { x + 0.8f, y + 0.8f, x + 4.44f - 0.8f, y + 4.24f, x + 0.8f, y + 8.48f - 0.8f };
    canvas_polyline(c, xy, 3, 1.7f, ink);
}

/* 1 px dashed outline inside the rectangle: 6 px on, 6 px off, as Figma's dash [6, 6]. */
static void dashed_frame(Canvas *c, int x, int y, int w, int h, uint8_t ink)
{
    for (int i = 0; i < w; i += 12) {
        int l = w - i < 6 ? w - i : 6;
        canvas_fill(c, x + i, y, l, 1, ink);
        canvas_fill(c, x + i, y + h - 1, l, 1, ink);
    }
    for (int i = 0; i < h; i += 12) {
        int l = h - i < 6 ? h - i : 6;
        canvas_fill(c, x, y + i, 1, l, ink);
        canvas_fill(c, x + w - 1, y + i, 1, l, ink);
    }
}

/* "Voce menu" component: 56 px, Plex Sans Medium 20 label, Plex Sans 16 value, "›" or "+".
 * The "+" row ("Crea nuova raccolta") has a dashed outline since v0.6. */
void ui_menu_row(Canvas *c, int x, int y, int w, const char *label, const char *value, MenuIcon icon, int focus)
{
    uint8_t bg = focus ? G_NERO : G_CARTA, fg = focus ? G_CARTA : G_NERO, fg2 = focus ? G_CARTA : G_GRAFITE;
    canvas_fill(c, x, y, w, 56, bg);
    if (icon == ICON_PLUS && !focus)
        dashed_frame(c, x, y, w, 56, G_NERO);
    int right = x + w - 20;
    if (icon == ICON_ARROW) {
        chevron(c, right - 4.44f, y + 28 - 4.24f, fg);
        right -= 5 + 16;
    } else if (icon == ICON_PLUS) {
        int pw = font_text_width(FACE_SANS, 16, "+");
        font_draw_text(c, FACE_SANS, 16, right - pw, ui_baseline(FACE_SANS, 16, y + 18, 20), "+", fg, 0);
        right -= pw + 16;
    }
    if (value && *value) {
        int vw = font_text_width(FACE_SANS, 16, value);
        font_draw_text(c, FACE_SANS, 16, right - vw, ui_baseline(FACE_SANS, 16, y + 18, 20), value, fg2, 0);
        right -= vw + 16;
    }
    font_draw_text(c, FACE_SANS_MEDIUM, 20, x + 20, ui_baseline(FACE_SANS_MEDIUM, 20, y + 15.5f, 25), label, fg,
                   right - x - 20);
}

/* Screen title ("Impostazioni"): Plex Sans SemiBold 28 - we use Medium, the closest face we carry. */
void ui_title(Canvas *c, int y, const char *title)
{
    font_draw_text(c, FACE_SANS_MEDIUM, 28, 32, ui_baseline(FACE_SANS_MEDIUM, 28, y + 24, 33.6f), title, G_NERO,
                   c->w - 64);
}

/* ---------------------------------------------------------------- text wrapping */

/* Splits s into at most max lines of width w. The last line keeps the rest of the text
 * (font_draw_text then ends it with "…"). Returns the number of lines; starts[] are byte offsets. */
static int wrap(Face f, int size, const char *s, int w, int max, int *starts, int *lens)
{
    int n = 0, len = (int)strlen(s), i = 0;
    while (i < len && n < max) {
        while (s[i] == ' ')
            i++;
        if (i >= len)
            break;
        if (n == max - 1) {
            starts[n] = i;
            lens[n++] = len - i;
            break;
        }
        int best = -1, j = i;
        char tmp[512];
        for (;;) {
            int k = j;
            while (k < len && s[k] != ' ')
                k++;
            int l = k - i < (int)sizeof tmp - 1 ? k - i : (int)sizeof tmp - 1;
            memcpy(tmp, s + i, l);
            tmp[l] = 0;
            if (font_text_width(f, size, tmp) > w)
                break;
            best = k;
            if (k >= len)
                break;
            j = k + 1;
        }
        if (best < 0) { /* a single word longer than the line: cut it by characters */
            int k = i;
            for (;;) {
                int step = 1;
                while (k + step < len && ((unsigned char)s[k + step] & 0xC0) == 0x80)
                    step++;
                int l = k + step - i < (int)sizeof tmp - 1 ? k + step - i : (int)sizeof tmp - 1;
                memcpy(tmp, s + i, l);
                tmp[l] = 0;
                if (k + step > len || (font_text_width(f, size, tmp) > w && k > i))
                    break;
                k += step;
                if (k >= len)
                    break;
            }
            best = k;
        }
        starts[n] = i;
        lens[n++] = best - i;
        i = best;
    }
    return n;
}

int ui_text_block(Canvas *c, Face f, int size, float lh, int x, float top, int w, int max_lines, const char *s,
                  uint8_t ink, int count_only)
{
    int st[8], ln[8];
    if (max_lines > 8)
        max_lines = 8;
    int n = wrap(f, size, s, w, max_lines, st, ln);
    if (count_only)
        return n;
    for (int i = 0; i < n; i++) {
        char line[512];
        int l = ln[i] < (int)sizeof line - 1 ? ln[i] : (int)sizeof line - 1;
        memcpy(line, s + st[i], l);
        line[l] = 0;
        font_draw_text(c, f, size, x, ui_baseline(f, size, top + i * lh, lh), line, ink, w);
    }
    return n;
}

/* Upper case for Latin text (also à è é ì ò ù). */
static char *upper(const char *s)
{
    size_t n = strlen(s);
    char *o = xmalloc(n * 2 + 1), *p = o;
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        i += utf8_decode(u + i, n - i, &cp);
        if (cp >= 'a' && cp <= 'z')
            cp -= 32;
        else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
            cp -= 32;
        p += utf8_encode(cp, p);
    }
    *p = 0;
    return o;
}

/* Group label of the settings ("LETTURA"): Plex Sans SemiBold 13 upper case, tracking 1.04 px, #222.
 * We use Medium, the closest face we carry. 'top' is the top of its 16 px line. */
void ui_group_label(Canvas *c, int x, float top, const char *label)
{
    char *u = upper(label);
    int base = ui_baseline(FACE_SANS_MEDIUM, 13, top, 16);
    float pen = x;
    const unsigned char *s = (const unsigned char *)u;
    size_t n = strlen(u), i = 0;
    uint32_t prev = 0;
    while (i < n) {
        uint32_t cp;
        i += utf8_decode(s + i, n - i, &cp);
        font_draw(c, FACE_SANS_MEDIUM, 13, (int)(pen * 64), base, cp, G_GRAFITE);
        pen += font_advance(FACE_SANS_MEDIUM, 13, prev, cp) / 64.0f + 1.04f;
        prev = 0;
    }
    free(u);
}

/* ---------------------------------------------------------------- covers */

/* Since v0.6 every book is drawn with one of Simone's publisher models (six since v0.6.3) (Figma, "Libreria
 * componenti" > "Copertine 600×800"): Sellerio, Adelphi, Einaudi, Feltrinelli, Bompiani, Mondadori.
 * Since v1.0 every model is drawn at 600 x 800, the screen of the Kindle: 1:1 in standby, scaled
 * to 172 x 229 in the Home. Colours are the Figma rgb turned into ink (255 - rgb). */

enum { M_SELLERIO, M_ADELPHI, M_EINAUDI, M_FELTRINELLI, M_BOMPIANI, M_MONDADORI, N_MODELS };

static int contains_ci(const char *s, const char *w)
{
    if (!s)
        return 0;
    size_t n = strlen(w);
    for (; *s; s++)
        if (strncasecmp(s, w, n) == 0)
            return 1;
    return 0;
}

/* The model of a publisher named in dc:publisher, or -1. */
static int publisher_model(const char *publisher)
{
    static const char *NAMES[N_MODELS] = { "sellerio", "adelphi", "einaudi", "feltrinelli", "bompiani", "mondadori" };
    for (int i = 0; i < N_MODELS; i++)
        if (contains_ci(publisher, NAMES[i]))
            return i;
    return -1;
}

/* The model: the publisher's own when dc:publisher names one of the five, otherwise always the same
 * one for the same book (FNV-1a of title and author). */
int cover_model_for(const char *title, const char *author, const char *publisher)
{
    int p = publisher_model(publisher);
    if (p >= 0)
        return p;
    uint32_t h = 2166136261u;
    const char *parts[3] = { title ? title : "", "|", author ? author : "" };
    for (int k = 0; k < 3; k++)
        for (const unsigned char *q = (const unsigned char *)parts[k]; *q; q++)
            h = (h ^ *q) * 16777619u;
    return (int)(h % N_MODELS);
}

/* Text of the covers: letter spacing (em fraction, as Figma's %), line height in px, left or centred,
 * word wrap on at most max lines (the last one ends with "…"). Returns the lines, or only counts them. */
typedef struct {
    Canvas *c;
    float ox, oy, s; /* origin of the 600 x 800 model and scale */
    int d;           /* v1.0: shift of every grey of the cover, in ink (steps of 17 = one e-ink grey) */
    uint32_t seed;   /* v1.0: seed of the illustration, from title and author */
} Model;

/* Every grey of a cover moves by the same d: background, panels, illustration and text together, so
 * the contrast between them stays the same (Simone, 2026-11-17: "le sfumature di grigio, mantenendo
 * sempre lo stesso contrasto"). Only pure black and pure white can be clamped. */
static uint8_t mink(const Model *m, int ink)
{
    ink += m->d;
    return (uint8_t)(ink < 0 ? 0 : ink > 255 ? 255 : ink);
}

static float tracked_width(Face f, int size, float track, const char *s, size_t len)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    int w64 = 0, n = 0;
    uint32_t prev = 0;
    while (i < len) {
        uint32_t cp;
        i += utf8_decode(u + i, len - i, &cp);
        w64 += font_advance(f, size, prev, cp);
        prev = cp;
        n++;
    }
    return w64 / 64.0f + (n > 1 ? (n - 1) * track : 0);
}

static void tracked_draw(Canvas *c, Face f, int size, float track, float x, int base, const char *s, size_t len,
                         uint8_t ink)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    float pen = x;
    uint32_t prev = 0;
    while (i < len) {
        uint32_t cp;
        i += utf8_decode(u + i, len - i, &cp);
        int adv = font_advance(f, size, prev, cp);
        font_draw(c, f, size, (int)lrintf(pen * 64) + (adv - font_advance(f, size, 0, cp)), base, cp, ink);
        pen += adv / 64.0f + track;
        prev = cp;
    }
}

static int model_text(const Model *m, Face f, float size_d, float track_em, float lh_pct, float x_d, float top_d,
                      float w_d, int center, int max_lines, const char *text, int upcase, uint8_t ink, int count_only)
{
    if (!text || !*text)
        return 0;
    char *t = upcase ? upper(text) : xstrdup(text);
    int size = (int)lrintf(size_d * m->s);
    if (size < 4)
        size = 4;
    float track = track_em * size, lh = size_d * m->s * lh_pct;
    float x = m->ox + x_d * m->s, top = m->oy + top_d * m->s, w = w_d * m->s;
    size_t len = strlen(t), i = 0;
    int lines = 0;
    while (i < len && lines < max_lines) {
        while (t[i] == ' ')
            i++;
        if (i >= len)
            break;
        /* longest run of words that fits */
        size_t end = i, j = i;
        for (;;) {
            size_t k = j;
            while (k < len && t[k] != ' ')
                k++;
            if (end > i && tracked_width(f, size, track, t + i, k - i) > w)
                break;
            end = k;
            if (k >= len)
                break;
            j = k + 1;
        }
        int last = lines == max_lines - 1 && end < len;
        char line[512];
        size_t l = (last ? len : end) - i;
        if (l > sizeof line - 8)
            l = sizeof line - 8;
        memcpy(line, t + i, l);
        line[l] = 0;
        if (tracked_width(f, size, track, line, l) > w) { /* cut and end with "…" */
            while (l > 0 && tracked_width(f, size, track, line, l) + font_text_width(f, size, "…") > w) {
                l--;
                while (l > 0 && ((unsigned char)line[l] & 0xC0) == 0x80)
                    l--;
            }
            while (l > 0 && line[l - 1] == ' ')
                l--;
            memcpy(line + l, "…", 4);
            l += 3;
        }
        if (!count_only) {
            float lw = tracked_width(f, size, track, line, l);
            float lx = center ? x + (w - lw) / 2 : x;
            tracked_draw(m->c, f, size, track, lx, ui_baseline(f, size, top + lines * lh, lh), line, l, mink(m, ink));
        }
        lines++;
        i = end;
        if (last)
            break;
    }
    free(t);
    return lines;
}

static void mrect(const Model *m, float x, float y, float w, float h, uint8_t ink)
{
    canvas_rectf(m->c, m->ox + x * m->s, m->oy + y * m->s, w * m->s, h * m->s, mink(m, ink));
}

static void mdisc(const Model *m, float cx, float cy, float r, uint8_t ink)
{
    canvas_disc(m->c, m->ox + cx * m->s, m->oy + cy * m->s, r * m->s, mink(m, ink));
}

static void mpoly(const Model *m, const float *xy, int n, uint8_t ink)
{
    float p[128];
    if (n > 64)
        n = 64;
    for (int i = 0; i < n; i++) {
        p[2 * i] = m->ox + xy[2 * i] * m->s;
        p[2 * i + 1] = m->oy + xy[2 * i + 1] * m->s;
    }
    canvas_polygon(m->c, p, n, mink(m, ink));
}

/* ------------------------------------------------ illustrations (v1.0) */

/* The "Illustrazione" rectangle of every model gets a drawing generated from the book: same book,
 * same drawing, on the Home and in standby. Eight motifs, each with its own random parameters; the
 * drawing uses the rectangle's grey moved by a fixed step (4 e-ink greys, 68), the accent by 8. All
 * shapes are rectangles, discs and small polygons: on the Kindle (soft float) big anti-aliased shapes
 * are slow. */

static uint32_t rnd(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return *r;
}

static float rndf(uint32_t *r, float a, float b) /* in [a, b) */
{
    return a + (b - a) * (float)(rnd(r) % 10000) / 10000.0f;
}

static const char *const MOTIFS[] = { "cerchi", "strisce", "punti", "sole", "montagne", "onde", "blocchi", "scacchi" };
#define N_MOTIFS 8

static void illustration(const Model *m, float x, float y, float w, float h, uint8_t base)
{
    mrect(m, x, y, w, h, base);
    int b = base;
    int dir = mink(m, base) < 128 ? 1 : -1;
    uint8_t k1 = (uint8_t)(b + dir * 68), k2 = (uint8_t)(b + dir * 136 < 0 ? 0 : b + dir * 136 > 255 ? 255 : b + dir * 136);
    uint32_t r = m->seed | 1;
    int motif = (int)(rnd(&r) % N_MOTIFS);

    /* drawing stays inside the rectangle */
    Canvas *c = m->c;
    int ox0 = c->cx0, oy0 = c->cy0, ox1 = c->cx1, oy1 = c->cy1;
    int ix0 = (int)lrintf(m->ox + x * m->s), iy0 = (int)lrintf(m->oy + y * m->s);
    int ix1 = (int)lrintf(m->ox + (x + w) * m->s), iy1 = (int)lrintf(m->oy + (y + h) * m->s);
    c->cx0 = ix0 > ox0 ? ix0 : ox0;
    c->cy0 = iy0 > oy0 ? iy0 : oy0;
    c->cx1 = ix1 < ox1 ? ix1 : ox1;
    c->cy1 = iy1 < oy1 ? iy1 : oy1;
    if (c->cx1 <= c->cx0 || c->cy1 <= c->cy0)
        goto done;

    float mn = w < h ? w : h;
    switch (motif) {
    case 0: { /* concentric circles, centre anywhere in the rectangle */
        float cx = x + rndf(&r, 0.2f, 0.8f) * w, cy = y + rndf(&r, 0.2f, 0.8f) * h;
        float R = mn * rndf(&r, 0.45f, 0.7f), step = R / (3 + rnd(&r) % 2);
        for (int i = 0; R - i * step > 4; i++)
            mdisc(m, cx, cy, R - i * step, i % 2 ? base : (i == 0 ? k1 : k2));
        break;
    }
    case 1: { /* bands, horizontal or vertical, of random widths */
        int vert = rnd(&r) % 2;
        float len = vert ? w : h, p = rndf(&r, 0, 20);
        while (p < len) {
            float t = rndf(&r, 6, 34), gap = rndf(&r, 8, 40);
            uint8_t k = rnd(&r) % 4 ? k1 : k2;
            if (vert)
                mrect(m, x + p, y, t, h, k);
            else
                mrect(m, x, y + p, w, t, k);
            p += t + gap;
        }
        break;
    }
    case 2: { /* grid of dots, growing towards one corner */
        float sp = rndf(&r, 34, 56), gx = rnd(&r) % 2, gy = rnd(&r) % 2;
        for (float py = sp / 2; py < h; py += sp)
            for (float px = sp / 2; px < w; px += sp) {
                float fx = gx ? px / w : 1 - px / w, fy = gy ? py / h : 1 - py / h;
                float rr = sp * 0.08f + sp * 0.36f * (fx + fy) / 2;
                mdisc(m, x + px, y + py, rr, k1);
            }
        break;
    }
    case 3: { /* sun over a horizon of bands */
        float hz = y + h * rndf(&r, 0.55f, 0.72f);
        float R = mn * rndf(&r, 0.18f, 0.3f);
        mdisc(m, x + w * rndf(&r, 0.25f, 0.75f), hz - R * rndf(&r, 0.2f, 1.1f), R, k2);
        mrect(m, x, hz, w, y + h - hz, k1);
        for (float p = hz + 10, t = 4; p < y + h; p += t * 3, t += 3)
            mrect(m, x, p, w, t, base);
        break;
    }
    case 4: { /* two ridges of mountains */
        for (int layer = 0; layer < 2; layer++) {
            float pts[2 * 12];
            int n = 0, peaks = 2 + rnd(&r) % 3;
            float top = y + h * (layer ? 0.55f : 0.3f);
            pts[n++] = x;
            pts[n++] = y + h;
            for (int i = 0; i <= peaks * 2; i++) {
                pts[n++] = x + w * i / (peaks * 2);
                pts[n++] = i % 2 ? top + rndf(&r, 0, h * 0.15f) : top + h * rndf(&r, 0.18f, 0.3f);
            }
            pts[n++] = x + w;
            pts[n++] = y + h;
            mpoly(m, pts, n / 2, layer ? k2 : k1);
        }
        break;
    }
    case 5: { /* stacked waves */
        float amp = rndf(&r, 8, 22), len = rndf(&r, 90, 180), sp = rndf(&r, 26, 44), ph = rndf(&r, 0, 6.28f);
        float th = sp * rndf(&r, 0.25f, 0.45f);
        for (float py = y + sp / 2; py < y + h + amp; py += sp) {
            float pts[2 * 50]; /* a band: the upper curve, then the lower one backwards */
            for (int i = 0; i < 25; i++) {
                float px = x - 10 + (w + 20) * i / 24;
                float wy = py + amp * sinf(ph + (px - x) * 6.2832f / len);
                pts[2 * i] = pts[2 * (49 - i)] = px;
                pts[2 * i + 1] = wy - th / 2;
                pts[2 * (49 - i) + 1] = wy + th / 2;
            }
            mpoly(m, pts, 50, k1);
            ph += rndf(&r, 0, 0.8f);
        }
        break;
    }
    case 6: { /* blocks: one big disc, a bar and a square */
        mdisc(m, x + w * rndf(&r, 0.3f, 0.7f), y + h * rndf(&r, 0.3f, 0.7f), mn * rndf(&r, 0.25f, 0.4f), k1);
        float bw = w * rndf(&r, 0.08f, 0.16f);
        mrect(m, x + w * rndf(&r, 0, 0.85f), y, bw, h, k2);
        float sq = mn * rndf(&r, 0.2f, 0.35f);
        mrect(m, x + rndf(&r, 0, w - sq), y + rndf(&r, 0, h - sq), sq, sq, k2);
        float bh = h * rndf(&r, 0.06f, 0.12f);
        mrect(m, x, y + h * rndf(&r, 0.1f, 0.9f), w, bh, k1);
        break;
    }
    default: { /* checkerboard with missing squares */
        int nx = 4 + rnd(&r) % 5;
        float sq = w / nx;
        for (int j = 0; j * sq < h; j++)
            for (int i = 0; i < nx; i++)
                if ((i + j) % 2 && rnd(&r) % 5)
                    mrect(m, x + i * sq, y + j * sq, sq, sq, rnd(&r) % 6 ? k1 : k2);
        break;
    }
    }
done:
    c->cx0 = ox0;
    c->cy0 = oy0;
    c->cx1 = ox1;
    c->cy1 = oy1;
}

/* Text height in model units, to push what follows down when a title takes more lines. */
static float lines_h(float size, float lh_pct, int n)
{
    return size * lh_pct * n;
}

/* The six models as Simone drew them in Figma at 600 x 800 ("Copertine 600×800", auto layout). The
 * illustration ("Illustrazione") sits where auto layout puts it: when a title takes more lines the
 * illustration moves or shrinks, as SPACE_BETWEEN would do. */

/* Space-between of one block of height h between top and bottom: returns its y, or top + min_gap when
 * there is no room. */
static float between(float top, float bottom, float h)
{
    float y = top + (bottom - top - h) / 2;
    return y < top ? top : y;
}

static void model_sellerio(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 600, 800, G_GRAFITE);                                                 /* #222 */
    int na = model_text(m, FACE_BODONI, 36, 0.12f, 1.3f, 52, 48, 496, 0, 2, au, 0, G_CARTA, 0);
    float ttop = 48 + lines_h(36, 1.3f, na) + 12;
    int nt = model_text(m, FACE_BODONI, 48, 0, 1.12f, 52, ttop, 496, 0, 3, t, 0, G_CARTA, 0);
    float head = ttop + lines_h(48, 1.12f, nt), foot = 740;
    float ih = 400;
    if (foot - head - 40 < ih)
        ih = foot - head - 40;
    if (ih > 40)
        illustration(m, 52, between(head, foot, ih), 360, ih, G_CARTA); /* white */
    model_text(m, FACE_BODONI, 30, 0, 1.25f, 52, 740, 496, 0, 1, ours ? "Sellerio" : pub, 0, G_CARTA, 0);
}

static void model_adelphi(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 600, 800, G_VELO);  /* #BBB */
    mrect(m, 22, 22, 556, 77, G_NERO); /* band with the series */
    model_text(m, FACE_BASK, 30, 0.12f, 1.23f, 38, 42, 524, 1, 1, ours ? "Gli Adelphi" : pub, 1, G_CARTA, 0);
    mrect(m, 22, 99, 556, 679, G_LEGGERO); /* #CCC panel */
    int na = model_text(m, FACE_BASK_ITALIC, 34, 0.10f, 1.3f, 52, 130, 496, 1, 2, au, 0, G_NERO, 0);
    float ttop = 130 + lines_h(34, 1.3f, na) + 20;
    int nt = model_text(m, FACE_BASK_ITALIC, 52, 0, 1.18f, 52, ttop, 496, 1, 3, t, 0, G_NERO, 0);
    float head = ttop + lines_h(52, 1.18f, nt) + 20, ih = 320;
    if (712 - head < ih)
        ih = 712 - head;
    if (ih > 40)
        illustration(m, 140, 712 - ih, 320, ih, G_CENERE); /* #666 */
}

static void model_einaudi(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 600, 800, G_VELO);
    int na = model_text(m, FACE_MONO_BOLD, 36, 0.10f, 1.3f, 42, 42, 516, 1, 2, au, 1, G_NERO, 0);
    float ttop = 42 + lines_h(36, 1.3f, na) + 16;
    int nt = model_text(m, FACE_MONO_BOLD, 38, 0, 1.18f, 42, ttop, 516, 1, 3, t, 1, G_GRAFITE, 0);
    float head = ttop + lines_h(38, 1.18f, nt), foot = 760, ih = 456;
    if (foot - head - 40 < ih)
        ih = foot - head - 40;
    if (ih > 40)
        illustration(m, 68, between(head, foot, ih), 464, ih, G_CENERE);
    model_text(m, FACE_MONO_BOLD, 24, 0.10f, 1.3f, 42, 760, 516, 1, 1, ours ? "einaudi" : pub, 1, G_CARTA, 0);
}

static void model_feltrinelli(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 600, 800, G_SMOG);      /* #666 at the top edge */
    mrect(m, 0, 29, 600, 771, G_LEGGERO);  /* #CCC panel */
    int na = model_text(m, FACE_MONO_BOLD, 38, 0.10f, 1.3f, 82, 0, 466, 0, 2, au, 1, G_NERO, 1);
    int nt = model_text(m, FACE_MONO_BOLD, 40, 0, 1.18f, 82, 0, 466, 0, 2, t, 0, G_GRAFITE, 1);
    float nh = lines_h(38, 1.3f, na) + 4 + lines_h(40, 1.18f, nt);
    /* illustration from 73 (square since v1.0.2, as in Simone's reference), then name and title, then the
     * logo at 708: equal gaps */
    float ih = 496, gap = (708 - 73 - ih - nh) / 2;
    if (gap < 24) {
        gap = 24;
        ih = 708 - 73 - nh - 2 * gap;
    }
    if (ih > 40)
        illustration(m, 52, 73, 496, ih, G_FUMO); /* #AAA */
    float ntop = 73 + ih + gap;
    model_text(m, FACE_MONO_BOLD, 38, 0.10f, 1.3f, 82, ntop, 466, 0, 2, au, 1, G_NERO, 0);
    model_text(m, FACE_MONO_BOLD, 40, 0, 1.18f, 82, ntop + lines_h(38, 1.3f, na) + 4, 466, 0, 2, t, 0, G_GRAFITE, 0);
    float tri[6] = { m->ox + 52 * m->s, m->oy + 735 * m->s, m->ox + 77 * m->s, m->oy + 710 * m->s,
                     m->ox + 77 * m->s, m->oy + 760 * m->s };
    canvas_polygon(m->c, tri, 3, mink(m, G_NERO));
    if (ours) {
        static const char *L[3] = { "universale", "economica", "feltrinelli" };
        for (int i = 0; i < 3; i++)
            model_text(m, FACE_MONO_BOLD, 15, 0.10f, 1.2f, 88, 708 + 18 * i, 300, 0, 1, L[i], 1, G_NERO, 0);
    } else
        model_text(m, FACE_MONO_BOLD, 15, 0.10f, 1.2f, 88, 726, 460, 0, 1, pub, 1, G_NERO, 0);
}

static void model_bompiani(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 600, 800, G_VELO);
    mrect(m, 4, 4, 592, 792, G_CARTA);     /* white, 4 px #BBB outline */
    illustration(m, 4, 194, 592, 226, G_CENERE); /* "Illustrazione" band, #666 */
    mdisc(m, 300, 416, 38, 38); /* #D9D9D9 */
    model_text(m, FACE_BASK, 36, 0.10f, 1.3f, 54, 70, 492, 1, 2, au, 1, G_NERO, 0);
    model_text(m, FACE_BASK, 44, 0, 1.18f, 54, 494, 492, 1, 3, t, 0, G_GRAFITE, 0);
    model_text(m, FACE_BASK, 16, 0.10f, 1.25f, 54, 726, 492, 1, 1, ours ? "Bompiani" : pub, 1, G_NERO, 0);
}

/* Mondadori (v0.6.3): Simone's first model drawn directly at 600 x 800 ("Copertine 600×800" in Figma,
 * auto layout). Head 134 px with the round logo and "Oscar / Libri", centre with author, title and the
 * illustration at the bottom, foot with the publisher. */
static void model_mondadori(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 600, 800, 68); /* #BBB */
    mdisc(m, 55, 67, 32, G_GRAFITE); /* logo */
    if (ours) {
        model_text(m, FACE_MONO_BOLD, 16, 0.09f, 1.3f, 100, 38, 477, 0, 1, "Oscar", 1, G_NERO, 0);
        model_text(m, FACE_MONO_BOLD, 16, 0.09f, 1.3f, 100, 59, 477, 0, 1, "Libri", 1, G_NERO, 0);
    }
    int na = model_text(m, FACE_BASK, 32, 0.07f, 1.3f, 35, 173, 530, 0, 2, au, 0, G_NERO, 0);
    float ttop = 173 + lines_h(32, 1.3f, na) + 10;
    int nt = model_text(m, FACE_BASK, 40, 0.07f, 1.3f, 35, ttop, 530, 0, 3, t, 0, G_NERO, 0);
    float ill_top = 376, below = ttop + lines_h(40, 1.3f, nt) + 30;
    if (below > ill_top)
        ill_top = below;
    if (684 - ill_top > 40)
        illustration(m, 35, ill_top, 530, 684 - ill_top, G_CENERE); /* #666 */
    model_text(m, FACE_BASK, 16, 0, 1.25f, 23, 752, 554, 0, 1, ours ? "Mondadori" : pub, 1, G_NERO, 0);
}

static uint32_t cover_seed(const char *t, const char *au)
{
    uint32_t h = 2166136261u;
    const char *parts[3] = { t, "#", au };
    for (int k = 0; k < 3; k++)
        for (const unsigned char *q = (const unsigned char *)parts[k]; *q; q++)
            h = (h ^ *q) * 16777619u;
    return h;
}

static int cover_shift(int model, uint32_t seed)
{
    int step = (int)((seed >> 24) % 3) * 17;
    return model == M_SELLERIO ? step : model == M_BOMPIANI ? 0 : -step;
}

/* For the test sheet: "Einaudi · onde · -1" (model, motif, grey step). */
void cover_describe(const char *title, const char *author, const char *publisher, char *out, size_t n)
{
    static const char *NAMES[N_MODELS] = { "Sellerio", "Adelphi", "Einaudi", "Feltrinelli", "Bompiani", "Mondadori" };
    const char *t = title && *title ? title : "Senza titolo", *au = author ? author : "";
    int model = cover_model_for(title, author, publisher);
    uint32_t seed = cover_seed(t, au), r = seed | 1;
    int motif = (int)(rnd(&r) % N_MOTIFS);
    snprintf(out, n, "%s · %s · %+d", NAMES[model], MOTIFS[motif], cover_shift(model, seed) / 17);
}

/* Draws the cover of a book in the box (x, y, w, h). border: width of a black outline (0 = none);
 * the white Bompiani always gets at least 1 px, or it would vanish on the white page. */
void cover_draw(Canvas *c, float x, float y, float w, float h, const char *title, const char *author,
                const char *publisher, float border)
{
    int model = cover_model_for(title, author, publisher);
    /* the publisher's own model shows its logo as in Figma; another publisher's name goes in its place */
    int ours = publisher_model(publisher) == model;
    const char *pub = publisher && !ours ? publisher : "";
    const char *t = title && *title ? title : "Senza titolo";
    const char *au = author ? author : "";

    int ox0 = c->cx0, oy0 = c->cy0, ox1 = c->cx1, oy1 = c->cy1;
    int ix = (int)lrintf(x), iy = (int)lrintf(y), iw = (int)lrintf(x + w) - ix, ih = (int)lrintf(y + h) - iy;
    int cx0 = ix > ox0 ? ix : ox0, cy0 = iy > oy0 ? iy : oy0;
    int cx1 = ix + iw < ox1 ? ix + iw : ox1, cy1 = iy + ih < oy1 ? iy + ih : oy1;
    if (cx1 <= cx0 || cy1 <= cy0)
        return;
    c->cx0 = cx0;
    c->cy0 = cy0;
    c->cx1 = cx1;
    c->cy1 = cy1;

    /* all models are 600 x 800, the screen: 1:1 in standby, scaled in the Home (172 x 229) */
    float s = w / 600 < h / 800 ? w / 600 : h / 800;
    /* v1.0: the greys move together by 0, 1 or 2 e-ink steps, chosen by the book: darker for Sellerio
     * (its white text gets a touch of grey), lighter for the others (black text a touch of grey);
     * Bompiani stays on pure white. The seed also chooses the illustration. */
    uint32_t h1 = cover_seed(t, au);
    int d = cover_shift(model, h1);
    Model m = { c, x + (w - 600 * s) / 2, y + (h - 800 * s) / 2, s, d, h1 };
    static const uint8_t BG[N_MODELS] = { G_GRAFITE, G_VELO, G_VELO, G_LEGGERO, G_CARTA, G_VELO };
    canvas_fill(c, ix, iy, iw, ih, mink(&m, BG[model]));
    switch (model) {
    case M_SELLERIO: model_sellerio(&m, t, au, pub, ours); break;
    case M_ADELPHI: model_adelphi(&m, t, au, pub, ours); break;
    case M_EINAUDI: model_einaudi(&m, t, au, pub, ours); break;
    case M_FELTRINELLI: model_feltrinelli(&m, t, au, pub, ours); break;
    case M_MONDADORI: model_mondadori(&m, t, au, pub, ours); break;
    default: model_bompiani(&m, t, au, pub, ours); break;
    }
    int b = border > 0 ? (int)lrintf(border) : 0;
    if (b)
        canvas_frame(c, ix, iy, iw, ih, b, G_NERO);
    c->cx0 = ox0;
    c->cy0 = oy0;
    c->cx1 = ox1;
    c->cy1 = oy1;
}
