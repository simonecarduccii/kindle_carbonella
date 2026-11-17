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
 * componenti" > Frame 6): Sellerio, Adelphi, Einaudi, Feltrinelli, Bompiani. Each model is designed at
 * 240 x 347 and scaled uniformly; when the box has another ratio (standby, 600 x 800) the model is
 * centred and the rest is filled with its background. Colours are the Figma rgb turned into ink (255 - rgb). */

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
    float ox, oy, s; /* origin of the 240 x 347 model and scale */
} Model;

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
            tracked_draw(m->c, f, size, track, lx, ui_baseline(f, size, top + lines * lh, lh), line, l, ink);
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
    canvas_rectf(m->c, m->ox + x * m->s, m->oy + y * m->s, w * m->s, h * m->s, ink);
}

/* Text height in model units, to push what follows down when a title takes more lines. */
static float lines_h(float size, float lh_pct, int n)
{
    return size * lh_pct * n;
}

static void model_sellerio(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 240, 348, 221);                                                   /* #222 */
    model_text(m, FACE_BODONI, 14, 0.12f, 1.3f, 21, 19, 198, 0, 1, au, 0, G_CARTA, 0); /* author */
    int n = model_text(m, FACE_BODONI, 17, 0, 1.12f, 21, 40, 198, 0, 3, t, 0, G_CARTA, 0);
    float frame_top = 40 + lines_h(17, 1.12f, n) + 37; /* "Cornice bianca": 96 with a title of one line */
    if (frame_top < 96)
        frame_top = 96;
    float frame_h = 275 - frame_top;
    if (frame_h > 20)
        mrect(m, 21, frame_top, 144, frame_h, G_CARTA);
    model_text(m, FACE_BODONI, 12, 0, 1.25f, 21, 318, 198, 0, 1, ours ? "Sellerio" : pub, 0, G_CARTA, 0);
}

static void model_adelphi(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 240, 347, 68);        /* #BBB */
    mrect(m, 9, 9, 222, 31, G_NERO);     /* band with the series */
    model_text(m, FACE_BASK, 12, 0.12f, 1.25f, 17, 17, 206, 1, 1, ours ? "Gli Adelphi" : pub, 1, G_CARTA, 0);
    mrect(m, 9, 40, 222, 298, G_LEGGERO); /* #CCC */
    model_text(m, FACE_BASK_ITALIC, 13, 0.10f, 1.3f, 21, 61, 198, 1, 1, au, 0, G_NERO, 0);
    int n = model_text(m, FACE_BASK_ITALIC, 20, 0, 1.18f, 21, 88, 198, 1, 3, t, 0, G_NERO, 0);
    float ill_top = 170, below = 88 + lines_h(20, 1.18f, n) + 12;
    if (below > ill_top)
        ill_top = below;
    if (304 - ill_top > 20)
        mrect(m, 56, ill_top, 128, 304 - ill_top, 68); /* illustration */
}

static void model_einaudi(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 240, 347, 68);
    model_text(m, FACE_MONO_BOLD, 15, 0.10f, 1.3f, 17, 34, 216, 1, 1, au, 1, G_NERO, 0);
    int n = model_text(m, FACE_MONO_BOLD, 15, 0, 1.18f, 21, 56, 198, 1, 3, t, 1, G_GRAFITE, 0);
    float ill_top = 109, below = 56 + lines_h(15, 1.18f, n) + 17;
    if (below > ill_top)
        ill_top = below;
    if (310 - ill_top > 20)
        mrect(m, 27, ill_top, 186, 310 - ill_top, G_SMOG); /* #999 */
    model_text(m, FACE_MONO_BOLD, 10, 0.10f, 1.3f, 17, 327, 206, 1, 1, ours ? "einaudi" : pub, 1, G_CARTA, 0);
}

static void model_feltrinelli(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 240, 347, 68);
    mrect(m, 0, 8, 240, 339, G_LEGGERO);
    mrect(m, 21, 27, 198, 201, G_FUMO); /* #AAA */
    int na = model_text(m, FACE_MONO_BOLD, 15, 0.10f, 1.3f, 33, 246, 173, 0, 2, au, 1, G_NERO, 1);
    int nt = model_text(m, FACE_MONO_BOLD, 16, 0, 1.18f, 33, 0, 173, 0, 2, t, 0, G_GRAFITE, 1);
    /* name and title sit above the logo: they grow upwards when they take more lines */
    float top = 246, end = top + lines_h(15, 1.3f, na) + lines_h(16, 1.18f, nt);
    if (end > 300)
        top -= end - 300;
    model_text(m, FACE_MONO_BOLD, 15, 0.10f, 1.3f, 33, top, 173, 0, 2, au, 1, G_NERO, 0);
    model_text(m, FACE_MONO_BOLD, 16, 0, 1.18f, 33, top + lines_h(15, 1.3f, na), 173, 0, 2, t, 0, G_GRAFITE, 0);
    /* logo: the triangle, then "universale economica feltrinelli" on three lines */
    float tri[6] = { m->ox + 20 * m->s, m->oy + 319 * m->s, m->ox + 29.9f * m->s, m->oy + 309.1f * m->s,
                     m->ox + 29.9f * m->s, m->oy + 328.9f * m->s };
    canvas_polygon(m->c, tri, 3, G_NERO);
    if (ours) {
        static const char *L[3] = { "universale", "economica", "feltrinelli" };
        for (int i = 0; i < 3; i++)
            model_text(m, FACE_MONO_BOLD, 6, 0.10f, 1.3f, 34, 308 + 7 * i, 120, 0, 1, L[i], 1, G_NERO, 0);
    } else
        model_text(m, FACE_MONO_BOLD, 6, 0.10f, 1.3f, 34, 315, 180, 0, 1, pub, 1, G_NERO, 0);
}

static void model_bompiani(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 240, 347, 68);
    mrect(m, 1, 1, 238, 345, G_CARTA);
    mrect(m, 0, 82, 240, 98, 68); /* band */
    canvas_disc(m->c, m->ox + 120 * m->s, m->oy + 184 * m->s, 15 * m->s, 38); /* #D9D9D9 */
    model_text(m, FACE_BASK, 14, 0.10f, 1.3f, 20, 33, 200, 1, 2, au, 1, G_NERO, 0);
    model_text(m, FACE_BASK, 16, 0, 1.18f, 16, 211, 208, 1, 3, t, 0, G_GRAFITE, 0);
    model_text(m, FACE_BASK, 6, 0.10f, 1.3f, 20, 317, 200, 1, 1, ours ? "Bompiani" : pub, 1, G_NERO, 0);
}

/* Mondadori (v0.6.3): Simone's first model drawn directly at 600 x 800 ("Copertine 600×800" in Figma,
 * auto layout). Head 134 px with the round logo and "Oscar / Libri", centre with author, title and the
 * illustration at the bottom, foot with the publisher. */
static void model_mondadori(const Model *m, const char *t, const char *au, const char *pub, int ours)
{
    mrect(m, 0, 0, 600, 800, 68); /* #BBB */
    canvas_disc(m->c, m->ox + 55 * m->s, m->oy + 67 * m->s, 32 * m->s, G_GRAFITE); /* logo */
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
        mrect(m, 35, ill_top, 530, 684 - ill_top, G_CENERE); /* "Illustrazione" #666 */
    model_text(m, FACE_BASK, 16, 0, 1.25f, 23, 752, 554, 0, 1, ours ? "Mondadori" : pub, 1, G_NERO, 0);
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

    /* models are 240 x 347, Mondadori is already 600 x 800 */
    float bw = model == M_MONDADORI ? 600 : 240, bh = model == M_MONDADORI ? 800 : 347;
    float s = w / bw < h / bh ? w / bw : h / bh;
    Model m = { c, x + (w - bw * s) / 2, y + (h - bh * s) / 2, s };
    static const uint8_t BG[N_MODELS] = { 221, 68, 68, 68, G_CARTA, 68 };
    canvas_fill(c, ix, iy, iw, ih, BG[model]);
    switch (model) {
    case M_SELLERIO: model_sellerio(&m, t, au, pub, ours); break;
    case M_ADELPHI: model_adelphi(&m, t, au, pub, ours); break;
    case M_EINAUDI: model_einaudi(&m, t, au, pub, ours); break;
    case M_FELTRINELLI: model_feltrinelli(&m, t, au, pub, ours); break;
    case M_MONDADORI: model_mondadori(&m, t, au, pub, ours); break;
    default: model_bompiani(&m, t, au, pub, ours); break;
    }
    int b = border > 0 ? (int)lrintf(border) : model == M_BOMPIANI ? 1 : 0;
    if (b)
        canvas_frame(c, ix, iy, iw, ih, b, G_NERO);
    c->cx0 = ox0;
    c->cy0 = oy0;
    c->cx1 = ox1;
    c->cy1 = oy1;
}
