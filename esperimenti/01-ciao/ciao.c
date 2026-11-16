/*
 * Carbonella - esperimento 01: "Ciao" sullo schermo e-ink del Kindle 4.
 *
 * What it does:
 *   1. opens the framebuffer (/dev/fb0) and maps it into memory;
 *   2. paints the screen white and draws "Ciao" (plus "Carbonella")
 *      with a tiny hand-made bitmap font, scaled up;
 *   3. asks the e-ink controller to refresh the whole screen.
 *
 * No external libraries: only the Linux kernel interfaces.
 * Writing to the framebuffer cannot damage the Kindle: the worst case is
 * a screen that looks wrong until the next refresh.
 *
 * Usage on the Kindle:  ./ciao        (normal colours)
 *                       ./ciao -i     (inverted, if the screen comes out negative)
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* Legacy Kindle e-ink driver ("einkfb"), used by K2, K3 and K4. */
#define FBIO_EINK_UPDATE_DISPLAY 0x46db
#define FX_UPDATE_FULL 1

/* A 5x7 font, only for the letters we need. '#' = ink, '.' = paper. */
struct glyph {
    char ch;
    const char *rows[7];
};

static const struct glyph FONT[] = {
    { 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
    { 'a', { ".....", ".....", ".###.", "....#", ".####", "#...#", ".####" } },
    { 'b', { "#....", "#....", "#.##.", "##..#", "#...#", "#...#", "####." } },
    { 'e', { ".....", ".....", ".###.", "#...#", "#####", "#....", ".###." } },
    { 'i', { "..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###." } },
    { 'l', { ".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." } },
    { 'n', { ".....", ".....", "#.##.", "##..#", "#...#", "#...#", "#...#" } },
    { 'o', { ".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###." } },
    { 'r', { ".....", ".....", "#.##.", "##..#", "#....", "#....", "#...." } },
};

#define GLYPH_W 5
#define GLYPH_H 7

struct screen {
    uint8_t *mem;
    uint32_t width, height, bpp, stride;
    int invert;
};

static const struct glyph *find_glyph(char ch)
{
    for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
        if (FONT[i].ch == ch)
            return &FONT[i];
    return NULL; /* unknown characters are drawn as a space */
}

/* gray: 0 = black, 255 = white */
static void put_pixel(struct screen *s, uint32_t x, uint32_t y, uint8_t gray)
{
    if (x >= s->width || y >= s->height)
        return;
    if (s->invert)
        gray = 255 - gray;

    uint8_t *row = s->mem + (size_t)y * s->stride;
    switch (s->bpp) {
    case 4: { /* two pixels per byte, left pixel in the high nibble */
        uint8_t *p = row + x / 2;
        uint8_t v = gray >> 4;
        *p = (x & 1) ? (uint8_t)((*p & 0xF0) | v) : (uint8_t)((*p & 0x0F) | (v << 4));
        break;
    }
    case 8:
        row[x] = gray;
        break;
    case 16: {
        uint16_t v = (uint16_t)(((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3));
        ((uint16_t *)row)[x] = v;
        break;
    }
    case 32:
        ((uint32_t *)row)[x] = 0xFF000000u | gray * 0x010101u;
        break;
    }
}

static void fill(struct screen *s, uint8_t gray)
{
    for (uint32_t y = 0; y < s->height; y++)
        for (uint32_t x = 0; x < s->width; x++)
            put_pixel(s, x, y, gray);
}

/* Draws text centred horizontally, with its top edge at y. */
static void draw_text(struct screen *s, const char *text, uint32_t y, uint32_t scale)
{
    uint32_t n = (uint32_t)strlen(text);
    uint32_t advance = (GLYPH_W + 1) * scale; /* one empty column between letters */
    uint32_t total = n * advance - scale;
    uint32_t x0 = total < s->width ? (s->width - total) / 2 : 0;

    for (uint32_t i = 0; i < n; i++) {
        const struct glyph *g = find_glyph(text[i]);
        if (!g)
            continue;
        for (uint32_t gy = 0; gy < GLYPH_H; gy++)
            for (uint32_t gx = 0; gx < GLYPH_W; gx++) {
                if (g->rows[gy][gx] != '#')
                    continue;
                for (uint32_t dy = 0; dy < scale; dy++)
                    for (uint32_t dx = 0; dx < scale; dx++)
                        put_pixel(s, x0 + i * advance + gx * scale + dx,
                                  y + gy * scale + dy, 0);
            }
    }
}

static int refresh(int fd)
{
    if (ioctl(fd, FBIO_EINK_UPDATE_DISPLAY, FX_UPDATE_FULL) == 0) {
        puts("refresh: ioctl FBIO_EINK_UPDATE_DISPLAY ok");
        return 0;
    }
    printf("refresh: ioctl failed (%s), trying /proc/eink_fb/update_display\n", strerror(errno));

    FILE *f = fopen("/proc/eink_fb/update_display", "w");
    if (f) {
        int ok = fputs("1\n", f) >= 0;
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            puts("refresh: /proc/eink_fb/update_display ok");
            return 0;
        }
    }
    puts("refresh: failed. Try running:  eips ''");
    return -1;
}

int main(int argc, char **argv)
{
    int invert = (argc > 1 && strcmp(argv[1], "-i") == 0);

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/fb0");
        return 1;
    }

    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        perror("ioctl FBIOGET_*SCREENINFO");
        close(fd);
        return 1;
    }
    printf("framebuffer: id=%s %ux%u (virtual %ux%u) bpp=%u line_length=%u grayscale=%u\n",
           fix.id, var.xres, var.yres, var.xres_virtual, var.yres_virtual,
           var.bits_per_pixel, fix.line_length, var.grayscale);

    if (var.bits_per_pixel != 4 && var.bits_per_pixel != 8 &&
        var.bits_per_pixel != 16 && var.bits_per_pixel != 32) {
        fprintf(stderr, "unsupported bpp: %u\n", var.bits_per_pixel);
        close(fd);
        return 1;
    }

    uint8_t *mem = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    /* Draw on the visible page (yoffset), in case the driver uses double buffering. */
    struct screen s = {
        .mem = mem + (size_t)var.yoffset * fix.line_length,
        .width = var.xres,
        .height = var.yres,
        .bpp = var.bits_per_pixel,
        .stride = fix.line_length,
        .invert = invert,
    };

    fill(&s, 255);
    draw_text(&s, "Ciao", s.height / 2 - 7 * 20, 20);
    draw_text(&s, "Carbonella", s.height / 2 + 60, 6);

    int rc = refresh(fd) == 0 ? 0 : 2;

    munmap(mem, fix.smem_len);
    close(fd);
    return rc;
}
