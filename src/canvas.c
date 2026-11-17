/* Carbonella - an in-memory image ("ink" values: 0 paper, 255 black) and drawing primitives. */
#include "carbonella.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void canvas_init(Canvas *c, int w, int h)
{
    c->w = w;
    c->h = h;
    c->px = xcalloc((size_t)w * h, 1);
    canvas_noclip(c);
}

void canvas_noclip(Canvas *c)
{
    c->cx0 = c->cy0 = 0;
    c->cx1 = c->w;
    c->cy1 = c->h;
}

void canvas_clip(Canvas *c, int x, int y, int w, int h)
{
    c->cx0 = x < 0 ? 0 : x;
    c->cy0 = y < 0 ? 0 : y;
    c->cx1 = x + w > c->w ? c->w : x + w;
    c->cy1 = y + h > c->h ? c->h : y + h;
}

void canvas_clear(Canvas *c, uint8_t ink)
{
    memset(c->px, ink, (size_t)c->w * c->h);
}

void canvas_fill(Canvas *c, int x, int y, int w, int h, uint8_t ink)
{
    if (x < c->cx0) {
        w -= c->cx0 - x;
        x = c->cx0;
    }
    if (y < c->cy0) {
        h -= c->cy0 - y;
        y = c->cy0;
    }
    if (x + w > c->cx1)
        w = c->cx1 - x;
    if (y + h > c->cy1)
        h = c->cy1 - y;
    if (w <= 0 || h <= 0)
        return;
    for (int j = 0; j < h; j++)
        memset(c->px + (size_t)(y + j) * c->w + x, ink, w);
}

void canvas_hline(Canvas *c, int x, int y, int w, int thick, uint8_t ink)
{
    canvas_fill(c, x, y, w, thick, ink);
}

void canvas_frame(Canvas *c, int x, int y, int w, int h, int t, uint8_t ink)
{
    canvas_fill(c, x, y, w, t, ink);
    canvas_fill(c, x, y + h - t, w, t, ink);
    canvas_fill(c, x, y, t, h, ink);
    canvas_fill(c, x + w - t, y, t, h, ink);
}

void canvas_blit_mask(Canvas *c, int x, int y, const uint8_t *mask, int mw, int mh, uint8_t ink)
{
    for (int j = 0; j < mh; j++) {
        int yy = y + j;
        if (yy < c->cy0 || yy >= c->cy1)
            continue;
        uint8_t *row = c->px + (size_t)yy * c->w;
        const uint8_t *m = mask + (size_t)j * mw;
        for (int i = 0; i < mw; i++) {
            int xx = x + i;
            unsigned a = m[i];
            if (!a || xx < c->cx0 || xx >= c->cx1)
                continue;
            int bg = row[xx];
            row[xx] = (uint8_t)(bg + ((int)ink - bg) * (int)a / 255);
        }
    }
}

/* ---- anti-aliased shapes: every pixel of the bounding box is sampled 4x4 times ---- */

typedef int (*InsideFn)(const void *ctx, float x, float y);

static void blend(Canvas *c, int x, int y, uint8_t ink, int cov16)
{
    uint8_t *p = c->px + (size_t)y * c->w + x;
    *p = (uint8_t)(*p + ((int)ink - *p) * cov16 / 16);
}

static void fill_inside(Canvas *c, float x0, float y0, float x1, float y1, InsideFn in, const void *ctx, uint8_t ink)
{
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    if (ix0 < c->cx0) ix0 = c->cx0;
    if (iy0 < c->cy0) iy0 = c->cy0;
    if (ix1 > c->cx1) ix1 = c->cx1;
    if (iy1 > c->cy1) iy1 = c->cy1;
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            int cov = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                    cov += in(ctx, x + (i + 0.5f) / 4, y + (j + 0.5f) / 4);
            if (cov)
                blend(c, x, y, ink, cov);
        }
}

typedef struct { float cx, cy, r, r_in, cx2, cy2, r2; } Circ;

static int in_disc(const void *p, float x, float y)
{
    const Circ *k = p;
    float d = (x - k->cx) * (x - k->cx) + (y - k->cy) * (y - k->cy);
    return d <= k->r * k->r && d >= k->r_in * k->r_in;
}

static int in_crescent(const void *p, float x, float y)
{
    const Circ *k = p;
    float d2 = (x - k->cx2) * (x - k->cx2) + (y - k->cy2) * (y - k->cy2);
    return in_disc(p, x, y) && d2 > k->r2 * k->r2;
}

void canvas_disc(Canvas *c, float cx, float cy, float r, uint8_t ink)
{
    Circ k = { cx, cy, r, 0, 0, 0, 0 };
    fill_inside(c, cx - r, cy - r, cx + r, cy + r, in_disc, &k, ink);
}

void canvas_ring(Canvas *c, float cx, float cy, float r, float thick, uint8_t ink)
{
    Circ k = { cx, cy, r, r - thick, 0, 0, 0 };
    fill_inside(c, cx - r, cy - r, cx + r, cy + r, in_disc, &k, ink);
}

void canvas_crescent(Canvas *c, float cx, float cy, float r, float cx2, float cy2, float r2, uint8_t ink)
{
    Circ k = { cx, cy, r, 0, cx2, cy2, r2 };
    fill_inside(c, cx - r, cy - r, cx + r, cy + r, in_crescent, &k, ink);
}

typedef struct { const float *xy; int n; float t2; } Poly;

static int in_polygon(const void *p, float x, float y)
{
    const Poly *k = p;
    int in = 0;
    for (int i = 0, j = k->n - 1; i < k->n; j = i++) {
        float xi = k->xy[2 * i], yi = k->xy[2 * i + 1], xj = k->xy[2 * j], yj = k->xy[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            in = !in;
    }
    return in;
}

static int in_segment(const Poly *k, int i, float x, float y)
{
    float ax = k->xy[2 * i], ay = k->xy[2 * i + 1], bx = k->xy[2 * i + 2], by = k->xy[2 * i + 3];
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = ax + t * dx - x, ey = ay + t * dy - y;
    return ex * ex + ey * ey <= k->t2;
}

/* One segment of a polyline (k->n = index of the segment); a point shared with the previous
 * segment belongs to that one, so the joints are not painted twice. */
static int in_polyline_seg(const void *p, float x, float y)
{
    const Poly *k = p;
    return in_segment(k, k->n, x, y) && !(k->n > 0 && in_segment(k, k->n - 1, x, y));
}

static void bbox(const float *xy, int n, float pad, float *b)
{
    b[0] = b[2] = xy[0];
    b[1] = b[3] = xy[1];
    for (int i = 1; i < n; i++) {
        float x = xy[2 * i], y = xy[2 * i + 1];
        if (x < b[0]) b[0] = x;
        if (x > b[2]) b[2] = x;
        if (y < b[1]) b[1] = y;
        if (y > b[3]) b[3] = y;
    }
    b[0] -= pad;
    b[1] -= pad;
    b[2] += pad;
    b[3] += pad;
}

void canvas_polygon(Canvas *c, const float *xy, int n, uint8_t ink)
{
    float b[4];
    Poly k = { xy, n, 0 };
    bbox(xy, n, 0, b);
    fill_inside(c, b[0], b[1], b[2], b[3], in_polygon, &k, ink);
}

/* Segment by segment, each in its own small box: testing every segment for every pixel of the
 * whole box was very slow on the Kindle (7 s for a Home with the "Zigzag" cover). */
void canvas_polyline(Canvas *c, const float *xy, int n, float thick, uint8_t ink)
{
    for (int i = 0; i + 1 < n; i++) {
        float b[4];
        Poly k = { xy, i, thick * thick / 4 };
        bbox(xy + 2 * i, 2, thick / 2, b);
        fill_inside(c, b[0], b[1], b[2], b[3], in_polyline_seg, &k, ink);
    }
}

void canvas_rectf(Canvas *c, float x, float y, float w, float h, uint8_t ink)
{
    /* axis-aligned: exact coverage = horizontal overlap x vertical overlap */
    float x1 = x + w, y1 = y + h;
    int ix0 = (int)floorf(x), iy0 = (int)floorf(y), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    if (ix0 < c->cx0) ix0 = c->cx0;
    if (iy0 < c->cy0) iy0 = c->cy0;
    if (ix1 > c->cx1) ix1 = c->cx1;
    if (iy1 > c->cy1) iy1 = c->cy1;
    for (int py = iy0; py < iy1; py++) {
        float oy = fminf(py + 1, y1) - fmaxf(py, y);
        for (int px = ix0; px < ix1; px++) {
            float ox = fminf(px + 1, x1) - fmaxf(px, x);
            int cov = (int)(ox * oy * 16 + 0.5f);
            if (cov > 0)
                blend(c, px, py, ink, cov > 16 ? 16 : cov);
        }
    }
}

/* Saves as a normal grayscale image (PGM): paper becomes white. Used by the simulator. */
int canvas_save_pgm(const Canvas *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P5\n%d %d\n255\n", c->w, c->h);
    for (size_t i = 0; i < (size_t)c->w * c->h; i++)
        fputc(255 - c->px[i], f);
    return fclose(f);
}
