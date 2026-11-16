/* Carbonella - an in-memory image ("ink" values: 0 paper, 255 black) and drawing primitives. */
#include "carbonella.h"

#include <stdio.h>
#include <string.h>

void canvas_init(Canvas *c, int w, int h)
{
    c->w = w;
    c->h = h;
    c->px = xcalloc((size_t)w * h, 1);
}

void canvas_clear(Canvas *c, uint8_t ink)
{
    memset(c->px, ink, (size_t)c->w * c->h);
}

void canvas_fill(Canvas *c, int x, int y, int w, int h, uint8_t ink)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > c->w)
        w = c->w - x;
    if (y + h > c->h)
        h = c->h - y;
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
        if (yy < 0 || yy >= c->h)
            continue;
        uint8_t *row = c->px + (size_t)yy * c->w;
        const uint8_t *m = mask + (size_t)j * mw;
        for (int i = 0; i < mw; i++) {
            int xx = x + i;
            unsigned a = m[i];
            if (!a || xx < 0 || xx >= c->w)
                continue;
            int bg = row[xx];
            row[xx] = (uint8_t)(bg + ((int)ink - bg) * (int)a / 255);
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
