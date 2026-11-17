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
    canvas_fill(c, x - 2, 21, 2, 6, G_NERO);
    x -= 2 + 24;
    canvas_frame(c, x, 18, 24, 12, 2, G_NERO);
    if (pct > 0) {
        int fw = (int)lrintf(19.5f * (pct > 100 ? 100 : pct) / 100);
        canvas_fill(c, x + 3, 21, fw, 6, G_NERO);
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
        font_draw_text(c, FACE_MONO, 14, x + (w - tw) / 2, base, center, G_SMOG, 0);
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
    canvas_fill(c, x, bar_y, done, 3, G_GRAFITE);
}

/* "›" of the menu rows: 4.44 x 8.48 */
static void chevron(Canvas *c, float x, float y, uint8_t ink)
{
    float xy[6] = { x + 0.8f, y + 0.8f, x + 4.44f - 0.8f, y + 4.24f, x + 0.8f, y + 8.48f - 0.8f };
    canvas_polyline(c, xy, 3, 1.7f, ink);
}

/* "Voce menu" component: 56 px, Plex Sans Medium 20 label, Plex Sans 16 value, "›" or "+". */
void ui_menu_row(Canvas *c, int x, int y, int w, const char *label, const char *value, MenuIcon icon, int focus)
{
    uint8_t bg = focus ? G_NERO : G_CARTA, fg = focus ? G_CARTA : G_NERO, fg2 = focus ? G_CARTA : G_GRAFITE;
    canvas_fill(c, x, y, w, 56, bg);
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

enum {
    SCURO, MEDIO, CHIARO, SOLE, RIGHE, DIAGONALE, CERCHI, PUNTI, VIRGOLETTE, META, SCACCHI, ARCHI, CORNICE,
    ONDE, GRIGLIA, MONTAGNE, LUNA, COLONNE, QUADRATI, ZIGZAG, PIOGGIA, GRADINI, FASCE, N_STYLES
};

int cover_style_for(const char *key)
{
    uint32_t h = 2166136261u; /* FNV-1a: always the same style for the same book */
    for (const unsigned char *p = (const unsigned char *)key; *p; p++)
        h = (h ^ *p) * 16777619u;
    return (int)(h % N_STYLES);
}

/* Pioggia: centre x, centre y, length, dark (1 = black, 0 = #666). From the Figma component. */
static const float RAIN[][4] = {
    { 78.97f, 90.88f, 7.7f, 0 }, { 72.89f, 86.63f, 7.0f, 1 }, { 89.6f, 80.02f, 9.3f, 1 }, { 99.26f, 62.18f, 12.4f, 0 },
    { 34.55f, 85.71f, 16.2f, 0 }, { 69.76f, 92.72f, 13.2f, 0 }, { 41.44f, 125.63f, 10.8f, 0 }, { 85.16f, 73.09f, 7.7f, 1 },
    { 62.83f, 74.26f, 7.0f, 1 }, { 49.08f, 117.49f, 11.6f, 0 }, { 90.45f, 81.86f, 14.7f, 1 }, { 36.8f, 66.07f, 10.8f, 0 },
    { 26.35f, 82.27f, 13.9f, 1 }, { 131.95f, 73.81f, 10.8f, 0 }, { 72.39f, 114.03f, 10.8f, 0 }, { 51.87f, 69.12f, 13.9f, 0 },
    { 52.45f, 110.86f, 15.5f, 1 }, { 128.45f, 117.08f, 13.9f, 1 }, { 45.68f, 80.72f, 13.9f, 1 }, { 52.87f, 121.07f, 6.2f, 0 },
    { 89.9f, 81.22f, 7.0f, 1 }, { 73.73f, 117.1f, 12.4f, 0 }, { 35.33f, 112.78f, 16.2f, 0 }, { 24.03f, 62.16f, 13.9f, 1 },
    { 98.21f, 115.23f, 8.5f, 1 }, { 83.39f, 115.5f, 15.5f, 0 }, { 29.37f, 75.78f, 8.5f, 0 }, { 77.63f, 78.53f, 6.2f, 1 },
    { 74.03f, 105.92f, 10.1f, 1 }, { 105.07f, 97.81f, 9.3f, 0 }, { 31.12f, 115.25f, 7.0f, 0 }, { 43.59f, 70.02f, 6.2f, 1 },
    { 72.18f, 79.2f, 12.4f, 1 }, { 82.84f, 80.05f, 7.7f, 1 }, { 97.21f, 90.35f, 16.2f, 1 }, { 74.1f, 126.33f, 15.5f, 0 },
    { 78.85f, 105.07f, 14.7f, 1 }, { 84.46f, 126.75f, 13.2f, 1 },
};

typedef struct {
    Canvas *c;
    float x, y, s;
} Pen;

static void R(const Pen *p, float x, float y, float w, float h, uint8_t ink)
{
    canvas_rectf(p->c, p->x + x * p->s, p->y + y * p->s, w * p->s, h * p->s, ink);
}

static void D(const Pen *p, float cx, float cy, float r, uint8_t ink)
{
    canvas_disc(p->c, p->x + cx * p->s, p->y + cy * p->s, r * p->s, ink);
}

static void O(const Pen *p, float cx, float cy, float r, float t, uint8_t ink)
{
    float th = t * p->s < 2 ? 2 : t * p->s; /* e-ink: lines at least 2 px */
    canvas_ring(p->c, p->x + cx * p->s, p->y + cy * p->s, r * p->s, th, ink);
}

static void P(const Pen *p, const float *pts, int n, uint8_t ink)
{
    float xy[64];
    for (int i = 0; i < n && i < 32; i++) {
        xy[2 * i] = p->x + pts[2 * i] * p->s;
        xy[2 * i + 1] = p->y + pts[2 * i + 1] * p->s;
    }
    canvas_polygon(p->c, xy, n, ink);
}

static void L(const Pen *p, const float *pts, int n, float t, uint8_t ink)
{
    float xy[128];
    for (int i = 0; i < n && i < 64; i++) {
        xy[2 * i] = p->x + pts[2 * i] * p->s;
        xy[2 * i + 1] = p->y + pts[2 * i + 1] * p->s;
    }
    float th = t * p->s < 2 ? 2 : t * p->s;
    canvas_polyline(p->c, xy, n, th, ink);
}

static void wave(const Pen *p, float y, float amp, float period, int zig, uint8_t ink)
{
    float pts[128];
    int n = 0;
    float step = zig ? period / 2 : period / 8;
    for (float x = -period; x <= 160 + period && n < 64; x += step, n++) {
        pts[2 * n] = x;
        if (zig)
            pts[2 * n + 1] = y + ((n & 1) ? -amp : amp);
        else
            pts[2 * n + 1] = y + amp * sinf(x / period * 2 * (float)M_PI);
    }
    L(p, pts, n, 1.6f, ink);
}

static void decoration(const Pen *p, int st)
{
    switch (st) {
    case SOLE:
        D(p, 80, 93.44f, 40.56f, G_NERO);
        R(p, 17.6f, 93.44f - 0.78f, 124.8f, 1.56f, G_NERO);
        break;
    case RIGHE:
        for (int i = 0; i < 8; i++)
            R(p, 9.89f, 56 + i * 10.52f, 140.22f, 4.38f, G_NERO);
        break;
    case DIAGONALE: {
        float band[] = { 70, 56, 112, 56, 160, 146, 160, 226 };
        float tri[] = { 112, 56, 160, 56, 160, 146 };
        P(p, band, 4, G_CENERE);
        P(p, tri, 3, G_VELO);
        break;
    }
    case CERCHI:
        for (int i = 1; i <= 4; i++)
            O(p, 80, 95, 9.75f * i, 1.6f, G_NERO);
        break;
    case PUNTI:
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 11; k++) {
                int big = k == 4 || k == 9 || r == 3;
                D(p, 23.43f + 12 * k, 59 + 12 * r, big ? 3 : 1.71f, G_NERO);
            }
        break;
    case VIRGOLETTE: {
        int size = (int)lrintf(200 * p->s);
        font_draw_text(p->c, FACE_SEMIBOLD, size, (int)lrintf(p->x + 18 * p->s),
                       ui_baseline(FACE_SEMIBOLD, size, p->y + 24 * p->s, 200 * p->s), "\xE2\x80\x9C", G_CENERE, 0);
        break;
    }
    case META:
        R(p, 0, 0, 160, 118, G_NERO);
        D(p, 124, 118, 22, G_CARTA);
        break;
    case SCACCHI:
        for (int k = 0; k < 4; k++) {
            float x = 17.6f + 31.2f * k;
            R(p, x, 56, 15.6f, 15.6f, G_NERO);
            R(p, x + 15.6f, 71.6f, 15.6f, 15.6f, G_FUMO);
            R(p, x, 87.2f, 15.6f, 15.6f, G_NERO);
            R(p, x + 15.6f, 102.8f, 15.6f, 15.6f, G_FUMO);
            R(p, x, 118.4f, 15.6f, 15.6f, G_NERO);
        }
        break;
    case ARCHI:
        for (int i = 1; i <= 4; i++)
            O(p, 160, 154, 4 + 18 * i, 2.2f, G_SMOG);
        break;
    case CORNICE: {
        float d[] = { 80, 82, 90, 92, 80, 102, 70, 92 };
        R(p, 3, 2.5f, 154, 2, G_NERO);
        R(p, 3, 227.5f, 154, 2, G_NERO);
        R(p, 3, 2.5f, 2, 227, G_NERO);
        R(p, 155, 2.5f, 2, 227, G_NERO);
        R(p, 7, 6.5f, 146, 2, G_NERO);
        R(p, 7, 223.5f, 146, 2, G_NERO);
        R(p, 7, 6.5f, 2, 219, G_NERO);
        R(p, 151, 6.5f, 2, 219, G_NERO);
        P(p, d, 4, G_NERO);
        R(p, 32, 91.5f, 32, 2, G_NERO);
        R(p, 96, 91.5f, 32, 2, G_NERO);
        break;
    }
    case ONDE: {
        static const uint8_t col[] = { G_NERO, G_NERO, G_CENERE, G_NERO, G_CENERE };
        for (int i = 0; i < 5; i++)
            wave(p, 64 + 16 * i, 4, 16, 0, col[i]);
        break;
    }
    case GRIGLIA:
        for (int k = 0; k < 7; k++)
            R(p, 31.25f + 16.25f * k, 56, 1.625f, 78, G_CENERE);
        for (int k = 0; k < 5; k++)
            R(p, 15, 56 + 17.875f * k, 130, 1.625f, G_CENERE);
        R(p, 80, 91.75f, 32.5f, 35.75f, G_NERO);
        break;
    case MONTAGNE: {
        float grey[] = { 10, 134, 58, 80, 100, 134 };
        float black[] = { 48, 134, 92, 78, 99, 85, 106, 72, 113, 82, 118, 76, 160, 134 };
        D(p, 46, 64, 6.5f, G_NERO);
        P(p, grey, 3, G_FUMO);
        P(p, black, 7, G_NERO);
        break;
    }
    case LUNA:
        canvas_crescent(p->c, p->x + 80 * p->s, p->y + 96 * p->s, 26 * p->s, p->x + 94 * p->s, p->y + 86 * p->s,
                        22 * p->s, G_VELO);
        D(p, 34, 72, 1.3f, G_VELO);
        D(p, 132, 64, 1.3f, G_VELO);
        D(p, 124, 132, 1.3f, G_VELO);
        D(p, 44, 128, 1.3f, G_VELO);
        break;
    case COLONNE: {
        static const float top[] = { 56, 62.64f, 80.89f, 97.49f, 110.77f };
        static const uint8_t col[] = { G_NERO, G_GRAFITE, G_CENERE, G_FUMO, G_VELO };
        for (int i = 0; i < 5; i++)
            R(p, 13.62f + 26.553f * i, top[i], 26.553f, 134 - top[i], col[i]);
        break;
    }
    case QUADRATI:
        R(p, 41, 56, 78, 78, G_NERO);
        R(p, 48.8f, 63.8f, 62.4f, 62.4f, G_CARTA);
        R(p, 56.6f, 71.6f, 46.8f, 46.8f, G_NERO);
        R(p, 64.4f, 79.4f, 31.2f, 31.2f, G_CARTA);
        R(p, 72.2f, 87.2f, 15.6f, 15.6f, G_NERO);
        break;
    case ZIGZAG: {
        static const uint8_t col[] = { G_CENERE, G_CENERE, G_CARTA, G_CENERE, G_CENERE };
        for (int i = 0; i < 5; i++)
            wave(p, 66 + 13 * i, 3.2f, 10, 1, col[i]);
        break;
    }
    case PIOGGIA: {
        const float sn = 0.2588f, cs = 0.9659f;
        for (size_t i = 0; i < sizeof RAIN / sizeof RAIN[0]; i++) {
            float h = RAIN[i][2] / 2;
            float pts[4] = { RAIN[i][0] + sn * h, RAIN[i][1] - cs * h, RAIN[i][0] - sn * h, RAIN[i][1] + cs * h };
            L(p, pts, 2, 1.55f, RAIN[i][3] ? G_NERO : G_CENERE);
        }
        break;
    }
    case GRADINI:
        for (int i = 0; i < 5; i++)
            R(p, 10.67f + 23.14f * i, 56 + 15.6f * i, 138.67f - 23.14f * i, 15.6f, (i & 1) ? G_GRAFITE : G_NERO);
        break;
    case FASCE: {
        static const uint8_t col[] = { G_NERO, G_GRAFITE, G_CENERE, G_FUMO };
        for (int i = 0; i < 4; i++)
            R(p, 0, 36 * i, 160, 36, col[i]);
        break;
    }
    }
}

/* Draws a generated cover in the rectangle (x, y, w, h); h may be taller than the 160:232 ratio
 * (the title stays at the bottom). border: width of the black outline of light covers (0 = as Figma, 1 px). */
void cover_draw(Canvas *c, float x, float y, float w, float h, const char *title, const char *author, int style,
                float border)
{
    int st = style % N_STYLES;
    float s = w / 160;
    uint8_t bg = G_NERO;
    int outline = 0;
    switch (st) {
    case VIRGOLETTE: bg = G_GRAFITE; break;
    case PUNTI: case CORNICE: case GRIGLIA: case PIOGGIA: bg = G_VELO; break;
    case CERCHI: case QUADRATI: bg = G_FUMO; break;
    case SOLE: case RIGHE: case META: case SCACCHI: case ONDE: case MONTAGNE: case COLONNE: case GRADINI:
    case FASCE: bg = G_CARTA; outline = 1; break;
    case CHIARO: bg = G_VELO; outline = 1; break;
    case MEDIO: bg = G_SMOG; break;
    }
    int light_author = st == SCURO || st == DIAGONALE || st == VIRGOLETTE || st == META || st == ARCHI ||
                       st == LUNA || st == ZIGZAG || st == FASCE;
    int light_title = st == SCURO || st == DIAGONALE || st == VIRGOLETTE || st == ARCHI || st == LUNA ||
                      st == ZIGZAG;

    int ox0 = c->cx0, oy0 = c->cy0, ox1 = c->cx1, oy1 = c->cy1;
    int ix = (int)lrintf(x), iy = (int)lrintf(y), iw = (int)lrintf(x + w) - ix, ih = (int)lrintf(y + h) - iy;
    /* intersect with the current clip */
    int cx0 = ix > ox0 ? ix : ox0, cy0 = iy > oy0 ? iy : oy0;
    int cx1 = ix + iw < ox1 ? ix + iw : ox1, cy1 = iy + ih < oy1 ? iy + ih : oy1;
    if (cx1 <= cx0 || cy1 <= cy0)
        return;
    c->cx0 = cx0;
    c->cy0 = cy0;
    c->cx1 = cx1;
    c->cy1 = cy1;

    canvas_fill(c, ix, iy, iw, ih, bg);
    Pen p = { c, (float)ix, (float)iy, s };
    decoration(&p, st);

    float pad_x = 14 * s;
    int tw = (int)(w - 2 * pad_x);
    int asz = (int)lrintf(13 * s), tsz = (int)lrintf(19 * s);
    char *au = upper(author && *author ? author : "");
    if (*au)
        ui_text_block(c, FACE_MONO_MEDIUM, asz, 1.2f * asz, (int)lrintf(ix + pad_x), iy + 16 * s, tw, 2, au,
                      light_author ? G_CARTA : G_NERO, 0);
    free(au);
    float lh = 1.15f * tsz;
    const char *t = title && *title ? title : "Senza titolo";
    int n = ui_text_block(c, FACE_SEMIBOLD, tsz, lh, 0, 0, tw, 4, t, 0, 1);
    float rule_y = iy + ih - 14 * s - 3 * s;
    float ttop = rule_y - 10 * s - n * lh;
    uint8_t tcol = light_title ? G_CARTA : G_NERO;
    ui_text_block(c, FACE_SEMIBOLD, tsz, lh, (int)lrintf(ix + pad_x), ttop, tw, 4, t, tcol, 0);
    canvas_rectf(c, ix + pad_x, rule_y, 24 * s, 3 * s, tcol);

    if (outline) {
        int b = border > 0 ? (int)lrintf(border) : 1;
        canvas_frame(c, ix, iy, iw, ih, b, G_NERO);
    }
    c->cx0 = ox0;
    c->cy0 = oy0;
    c->cx1 = ox1;
    c->cy1 = oy1;
}
