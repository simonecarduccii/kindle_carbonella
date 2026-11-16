/*
 * Carbonella - fonts.
 *
 * TrueType rendering with stb_truetype (one header file, public domain): far lighter than FreeType
 * and enough for our needs. The font is Literata (SIL Open Font License), designed by TypeTogether
 * for reading on screens, reduced to Latin characters to save space and memory.
 *
 * Every glyph is rendered once and kept in a cache (key: face, size, sub-pixel position, code point).
 * Anti-aliased pixels pass through a "darkening" curve: on e-ink light greys disappear, so thin
 * strokes are pushed towards black.
 */
#include "carbonella.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb/stb_truetype.h"

static const char *FILES[FACE_COUNT] = { "Literata-Regular.ttf", "Literata-Italic.ttf",
                                         "Literata-Bold.ttf", "Literata-BoldItalic.ttf" };

static struct {
    unsigned char *data;
    stbtt_fontinfo info;
    int ascent, descent, linegap;
    int ok;
} F[FACE_COUNT];

static uint8_t darken[256];

typedef struct {
    uint64_t key; /* 0 = empty */
    int glyph;
    int adv64;   /* advance in 1/64 px */
    int has_bmp;
    int bx, by, bw, bh;
    uint8_t *bmp;
} Slot;

#define CACHE_SLOTS 16384
#define CACHE_MAX_USED 12000
static Slot *cache;
static int cache_used;

int font_load(const char *dir)
{
    int loaded = 0;
    for (int f = 0; f < FACE_COUNT; f++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, FILES[f]);
        size_t len;
        F[f].data = (unsigned char *)read_file(path, &len);
        if (!F[f].data || !stbtt_InitFont(&F[f].info, F[f].data, stbtt_GetFontOffsetForIndex(F[f].data, 0))) {
            log_msg("font %s not loaded", path);
            free(F[f].data);
            F[f].data = NULL;
            continue;
        }
        stbtt_GetFontVMetrics(&F[f].info, &F[f].ascent, &F[f].descent, &F[f].linegap);
        F[f].ok = 1;
        loaded++;
    }
    if (!F[FACE_REGULAR].ok)
        return -1;
    /* missing faces fall back to the closest one */
    for (int f = 1; f < FACE_COUNT; f++)
        if (!F[f].ok)
            F[f] = F[f == FACE_BOLD_ITALIC && F[FACE_BOLD].ok ? FACE_BOLD : FACE_REGULAR];
    for (int i = 0; i < 256; i++)
        darken[i] = (uint8_t)lrintf(255.0f * powf(i / 255.0f, 0.70f));
    cache = xcalloc(CACHE_SLOTS, sizeof(Slot));
    log_msg("fonts loaded: %d", loaded);
    return 0;
}

static void cache_flush(void)
{
    for (int i = 0; i < CACHE_SLOTS; i++)
        free(cache[i].bmp);
    memset(cache, 0, CACHE_SLOTS * sizeof(Slot));
    cache_used = 0;
}

static float scale_for(Face f, int size)
{
    /* size = pixel height of the em square, as in CSS/Figma */
    return stbtt_ScaleForMappingEmToPixels(&F[f].info, (float)size);
}

static int find_glyph(Face f, uint32_t cp)
{
    int g = stbtt_FindGlyphIndex(&F[f].info, (int)cp);
    if (g)
        return g;
    switch (cp) {
    case 0xA0: case 0x2007: case 0x2009: case 0x200A: case 0x202F: cp = ' '; break;
    case 0x2010: case 0x2011: cp = '-'; break;
    case 0x2018: case 0x2019: cp = '\''; break;
    case 0x201C: case 0x201D: cp = '"'; break;
    default: cp = '?';
    }
    return stbtt_FindGlyphIndex(&F[f].info, (int)cp);
}

static Slot *lookup(Face f, int size, int sub, uint32_t cp)
{
    uint64_t key = ((uint64_t)(f + 1) << 40) | ((uint64_t)(size & 0xFF) << 32) | ((uint64_t)sub << 24) |
                   (cp & 0xFFFFFF);
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 50) & (CACHE_SLOTS - 1);
    for (;;) {
        Slot *s = &cache[h];
        if (s->key == key)
            return s;
        if (s->key == 0)
            break;
        h = (h + 1) & (CACHE_SLOTS - 1);
    }
    if (cache_used >= CACHE_MAX_USED) {
        cache_flush();
        return lookup(f, size, sub, cp);
    }
    Slot *s = &cache[h];
    s->key = key;
    cache_used++;
    s->glyph = find_glyph(f, cp);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&F[f].info, s->glyph, &adv, &lsb);
    s->adv64 = (int)lrintf(adv * scale_for(f, size) * 64.0f);
    if (cp == 0xAD) /* soft hyphen: invisible unless the line breaks there */
        s->adv64 = 0;
    return s;
}

int font_advance(Face f, int size, uint32_t prev, uint32_t cp)
{
    Slot *s = lookup(f, size, 0, cp);
    int adv = s->adv64;
    if (prev) {
        int g1 = lookup(f, size, 0, prev)->glyph;
        int k = stbtt_GetGlyphKernAdvance(&F[f].info, g1, s->glyph);
        if (k)
            adv += (int)lrintf(k * scale_for(f, size) * 64.0f);
    }
    return adv;
}

void font_draw(Canvas *c, Face f, int size, int x64, int baseline, uint32_t cp, uint8_t ink)
{
    if (cp == ' ' || cp == 0xA0 || cp == 0xAD)
        return;
    int sub = (x64 & 63) >> 4; /* 4 sub-pixel positions are plenty */
    Slot *s = lookup(f, size, sub, cp);
    if (!s->has_bmp) {
        float sc = scale_for(f, size);
        float shift = sub / 4.0f;
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBoxSubpixel(&F[f].info, s->glyph, sc, sc, shift, 0, &x0, &y0, &x1, &y1);
        s->bx = x0;
        s->by = y0;
        s->bw = x1 - x0;
        s->bh = y1 - y0;
        if (s->bw > 0 && s->bh > 0) {
            s->bmp = xmalloc((size_t)s->bw * s->bh);
            stbtt_MakeGlyphBitmapSubpixel(&F[f].info, s->bmp, s->bw, s->bh, s->bw, sc, sc, shift, 0, s->glyph);
            for (int i = 0; i < s->bw * s->bh; i++)
                s->bmp[i] = darken[s->bmp[i]];
        }
        s->has_bmp = 1;
    }
    if (s->bmp)
        canvas_blit_mask(c, (x64 >> 6) + s->bx, baseline + s->by, s->bmp, s->bw, s->bh, ink);
}

int font_ascent(Face f, int size)
{
    return (int)lrintf(F[f].ascent * scale_for(f, size));
}

int font_descent(Face f, int size)
{
    return (int)lrintf(-F[f].descent * scale_for(f, size));
}

int font_text_width(Face f, int size, const char *s)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t n = strlen(s), i = 0;
    int w = 0;
    uint32_t prev = 0;
    while (i < n) {
        uint32_t cp;
        i += utf8_decode(u + i, n - i, &cp);
        w += font_advance(f, size, prev, cp);
        prev = cp;
    }
    return (w + 63) >> 6;
}

int font_draw_text(Canvas *c, Face f, int size, int x, int baseline, const char *s, uint8_t ink, int max_w)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t n = strlen(s), i = 0;
    int x64 = x << 6;
    int limit64 = max_w > 0 ? (x + max_w) << 6 : 0;
    int ell = max_w > 0 ? font_advance(f, size, 0, 0x2026) : 0;
    uint32_t prev = 0;
    if (max_w > 0 && font_text_width(f, size, s) <= max_w)
        limit64 = 0; /* fits: no need to check */
    while (i < n) {
        uint32_t cp;
        i += utf8_decode(u + i, n - i, &cp);
        int adv = font_advance(f, size, prev, cp);
        if (limit64 && x64 + adv + ell > limit64) {
            font_draw(c, f, size, x64, baseline, 0x2026, ink);
            x64 += ell;
            break;
        }
        font_draw(c, f, size, x64 + (adv - lookup(f, size, 0, cp)->adv64), baseline, cp, ink);
        x64 += adv;
        prev = cp;
    }
    return (x64 + 63) >> 6;
}
