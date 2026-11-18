/*
 * Carbonella - shows the canvas on the e-ink screen.
 *
 * Kindle 4: /dev/fb0 is "eink_fb", 600x800, 8 bits per pixel, 0 = white, 255 = black
 * (measured on 2026-11-16, see diario/2026-11-16-primo-ciao.md). That is exactly our canvas format,
 * so drawing is a plain memory copy.
 *
 * The legacy einkfb driver does not refresh by itself: we ask with an ioctl.
 *   partial refresh: fast, no flash, leaves a bit of ghosting
 *   full refresh:    black/white flash that cleans the panel
 *
 * Simulator: every refresh is saved as a numbered PGM image in a folder.
 */
#include "carbonella.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define FBIO_EINK_UPDATE_DISPLAY 0x46db
#define FBIO_EINK_UPDATE_DISPLAY_AREA 0x46dd
#define FX_UPDATE_PARTIAL 0
#define FX_UPDATE_FULL 1

struct update_area_t {
    int x1, y1, x2, y2;
    int which_fx;
    uint8_t *buffer;
};

enum { FLASH_AREA, FLASH_DISPLAY, FLASH_INVERT };

static struct {
    int fd;
    uint8_t *mem;
    size_t mem_len;
    uint8_t *visible;
    int w, h, bpp, stride;
    const char *sim_dir;
    int sim_count;
    int flash_mode;
    int area_ok; /* 0 unknown/1 works/-1 failed once */
} D = { .fd = -1 };

int display_width(void) { return D.w; }
int display_height(void) { return D.h; }

void display_set_flash_mode(const char *mode)
{
    if (!mode || strcmp(mode, "area") == 0)
        D.flash_mode = FLASH_AREA;
    else if (strcmp(mode, "display") == 0)
        D.flash_mode = FLASH_DISPLAY;
    else if (strcmp(mode, "inverti") == 0)
        D.flash_mode = FLASH_INVERT;
}

int display_open(const char *sim_dir)
{
    if (sim_dir) {
        D.sim_dir = sim_dir;
        D.w = 600;
        D.h = 800;
        mkdir_p(sim_dir);
        return 0;
    }
    D.fd = open("/dev/fb0", O_RDWR);
    if (D.fd < 0) {
        log_msg("open /dev/fb0: %s", strerror(errno));
        return -1;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(D.fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(D.fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        log_msg("fb screeninfo: %s", strerror(errno));
        return -1;
    }
    log_msg("framebuffer %s %ux%u bpp=%u line=%u", fix.id, var.xres, var.yres, var.bits_per_pixel,
            fix.line_length);
    if (var.bits_per_pixel != 8 && var.bits_per_pixel != 4) {
        log_msg("unsupported bpp %u", var.bits_per_pixel);
        return -1;
    }
    D.mem_len = fix.smem_len;
    D.mem = mmap(NULL, D.mem_len, PROT_READ | PROT_WRITE, MAP_SHARED, D.fd, 0);
    if (D.mem == MAP_FAILED) {
        log_msg("mmap: %s", strerror(errno));
        return -1;
    }
    D.w = var.xres;
    D.h = var.yres;
    D.bpp = var.bits_per_pixel;
    D.stride = fix.line_length;
    D.visible = D.mem + (size_t)var.yoffset * fix.line_length;
    return 0;
}

void display_close(void)
{
    if (D.mem && D.mem != MAP_FAILED)
        munmap(D.mem, D.mem_len);
    if (D.fd >= 0)
        close(D.fd);
    D.fd = -1;
    D.mem = NULL;
}

static void copy_area(const Canvas *c, Rect a, int invert)
{
    for (int y = a.y; y < a.y + a.h; y++) {
        const uint8_t *src = c->px + (size_t)y * c->w;
        uint8_t *dst = D.visible + (size_t)y * D.stride;
        if (D.bpp == 8) {
            if (!invert) {
                memcpy(dst + a.x, src + a.x, a.w);
            } else {
                for (int x = a.x; x < a.x + a.w; x++)
                    dst[x] = 255 - src[x];
            }
        } else { /* 4 bpp, two pixels per byte, left one in the high nibble */
            for (int x = a.x; x < a.x + a.w; x++) {
                uint8_t v = (uint8_t)((invert ? 255 - src[x] : src[x]) >> 4);
                uint8_t *p = dst + x / 2;
                *p = (x & 1) ? (uint8_t)((*p & 0xF0) | v) : (uint8_t)((*p & 0x0F) | (v << 4));
            }
        }
    }
}

static void eink_refresh(Rect a, int fx)
{
    if (D.area_ok >= 0) {
        struct update_area_t u = { a.x, a.y, a.x + a.w, a.y + a.h, fx, NULL };
        if (ioctl(D.fd, FBIO_EINK_UPDATE_DISPLAY_AREA, &u) == 0) {
            D.area_ok = 1;
            return;
        }
        log_msg("UPDATE_DISPLAY_AREA failed (%s), using UPDATE_DISPLAY", strerror(errno));
        D.area_ok = -1;
    }
    ioctl(D.fd, FBIO_EINK_UPDATE_DISPLAY, fx);
}

static Rect clip(Rect a)
{
    if (a.w <= 0 || a.h <= 0)
        a = (Rect){ 0, 0, D.w, D.h };
    if (a.x < 0) {
        a.w += a.x;
        a.x = 0;
    }
    if (a.y < 0) {
        a.h += a.y;
        a.y = 0;
    }
    if (a.x + a.w > D.w)
        a.w = D.w - a.x;
    if (a.y + a.h > D.h)
        a.h = D.h - a.y;
    return a;
}

void display_update(const Canvas *c, Rect area, RefreshKind kind)
{
    area = clip(area);
    if (D.sim_dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%03d-%s.pgm", D.sim_dir, D.sim_count++,
                 kind == REFRESH_FULL ? "full" : "partial");
        canvas_save_pgm(c, path);
        return;
    }
    if (!D.mem)
        return;
    double t0 = now_ms();
    if (kind == REFRESH_FULL) {
        Rect all = { 0, 0, D.w, D.h };
        switch (D.flash_mode) {
        case FLASH_INVERT: /* show the negative, then the positive: forces a clean panel */
            copy_area(c, all, 1);
            eink_refresh(all, FX_UPDATE_FULL);
            copy_area(c, all, 0);
            eink_refresh(all, FX_UPDATE_FULL);
            break;
        case FLASH_DISPLAY:
            copy_area(c, all, 0);
            ioctl(D.fd, FBIO_EINK_UPDATE_DISPLAY, FX_UPDATE_FULL);
            break;
        default:
            copy_area(c, all, 0);
            eink_refresh(all, FX_UPDATE_FULL);
        }
    } else {
        copy_area(c, area, 0);
        eink_refresh(area, FX_UPDATE_PARTIAL);
    }
    log_msg("refresh %s %dx%d+%d+%d %.0f ms", kind == REFRESH_FULL ? "full" : "partial", area.w, area.h,
            area.x, area.y, now_ms() - t0);
}
