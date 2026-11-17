/*
 * Carbonella - shared declarations.
 *
 * The program is split in small modules:
 *   util.c     config files, UTF-8, logging, clock
 *   canvas.c   an in-memory 600x800 image and drawing primitives
 *   display.c  sends the canvas to the e-ink screen (or to PGM files in the simulator)
 *   input.c    reads the Kindle keys from /dev/input (or from a script in the simulator)
 *   font.c     TrueType fonts (stb_truetype) with a glyph cache
 *   epub.c     opens EPUB and TXT files, turns XHTML into simple styled blocks
 *   layout.c   line breaking, justification, pagination
 *   library.c  finds the books, remembers positions and reading sessions
 *   app.c      screens (library, reader, menus) and main()
 */
#ifndef CARBONELLA_H
#define CARBONELLA_H

#include <stddef.h>
#include <stdint.h>

#define CARBONELLA_VERSION "1.0.3"

/* ------------------------------------------------------------------ util */

extern volatile int g_quit;  /* set by SIGTERM/SIGINT */
extern volatile int g_power; /* 1 = the Kindle is going to sleep (SIGUSR1), 2 = it woke up (SIGUSR2) */

void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
double now_ms(void);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *read_file(const char *path, size_t *len); /* NUL-terminated, NULL on error */
int write_file_atomic(const char *path, const char *data, size_t len);
int mkdir_p(const char *path);

/* Decodes one UTF-8 code point from s (at most n bytes). Returns bytes used (>=1). */
int utf8_decode(const unsigned char *s, size_t n, uint32_t *cp);
int utf8_encode(uint32_t cp, char *out); /* returns bytes written (1..4) */
int utf8_valid(const char *s, size_t n);
char *latin1_to_utf8(const char *s, size_t n, size_t *out_len);

/* Settings: "key = value" lines, '#' comments. */
#define MAX_SETTINGS 64
typedef struct {
    int n;
    char *key[MAX_SETTINGS];
    char *val[MAX_SETTINGS];
} Settings;

void settings_load(Settings *s, const char *path); /* adds/overrides */
const char *settings_get(const Settings *s, const char *key, const char *def);
int settings_int(const Settings *s, const char *key, int def);
void settings_set_int(Settings *s, const char *key, int v);
int settings_save(const Settings *s, const char *path, const char *header);

/* ---------------------------------------------------------------- canvas */

/* Values are "ink": 0 = paper (white), 255 = full black.
 * This is also exactly the Kindle 4 framebuffer format (eink_fb, 8 bpp). */
/* The grey scale of the Figma design ("E-ink" styles), as ink. Paper is Carta #EEE, never pure white. */
#define G_CARTA 0      /* paper: pure white (Simone, v0.3: maximum contrast) */
#define G_LEGGERO 51   /* #CCC */
#define G_VELO 68      /* #BBB */
#define G_FUMO 85      /* #AAA */
#define G_SMOG 102     /* #999 */
#define G_CENERE 153   /* #666 */
#define G_GRAFITE 221  /* #222 */
#define G_NERO 255     /* #000 */

#define INK_PAPER G_CARTA
#define INK_BLACK G_NERO
#define INK_DARK G_GRAFITE /* secondary text */
#define INK_MID G_CENERE   /* separators */

typedef struct {
    int w, h;
    uint8_t *px;
    int cx0, cy0, cx1, cy1; /* clip rectangle: drawing only happens inside it */
} Canvas;

typedef struct {
    int x, y, w, h;
} Rect;

void canvas_init(Canvas *c, int w, int h);
void canvas_clear(Canvas *c, uint8_t ink);
void canvas_fill(Canvas *c, int x, int y, int w, int h, uint8_t ink);
void canvas_frame(Canvas *c, int x, int y, int w, int h, int thick, uint8_t ink);
void canvas_hline(Canvas *c, int x, int y, int w, int thick, uint8_t ink);
/* Blends an 8-bit coverage mask with the given ink. */
void canvas_blit_mask(Canvas *c, int x, int y, const uint8_t *mask, int mw, int mh, uint8_t ink);
void canvas_clip(Canvas *c, int x, int y, int w, int h); /* drawing outside is ignored */
void canvas_noclip(Canvas *c);
/* Anti-aliased shapes (4x4 samples per pixel), coordinates in float pixels. */
void canvas_disc(Canvas *c, float cx, float cy, float r, uint8_t ink);
void canvas_ring(Canvas *c, float cx, float cy, float r, float thick, uint8_t ink); /* r = outer radius */
void canvas_crescent(Canvas *c, float cx, float cy, float r, float cx2, float cy2, float r2, uint8_t ink);
void canvas_polygon(Canvas *c, const float *xy, int n, uint8_t ink); /* n points, even-odd */
void canvas_polyline(Canvas *c, const float *xy, int n, float thick, uint8_t ink);
void canvas_rectf(Canvas *c, float x, float y, float w, float h, uint8_t ink);
int canvas_save_pgm(const Canvas *c, const char *path);

/* --------------------------------------------------------------- display */

typedef enum { REFRESH_PARTIAL, REFRESH_FULL } RefreshKind;

int display_open(const char *sim_dir); /* sim_dir NULL = real Kindle framebuffer */
void display_close(void);
/* Copies the given area of the canvas to the screen and refreshes it. */
void display_update(const Canvas *c, Rect area, RefreshKind kind);
int display_width(void);
int display_height(void);
void display_set_flash_mode(const char *mode);

/* ----------------------------------------------------------------- input */

typedef enum {
    K_NONE = 0,
    K_UP, K_DOWN, K_LEFT, K_RIGHT, K_SELECT,
    K_NEXT, K_PREV, /* page keys on the sides */
    K_HOME, K_MENU, K_BACK, K_KEYBOARD,
    K_TIMEOUT,      /* no key within the given time */
    K_SLEEP, K_WAKE, /* power button / standby (from carbonella.sh via SIGUSR1/SIGUSR2) */
    K_QUIT,         /* end of simulator script */
} Key;

int input_open(const char *sim_script); /* sim_script NULL = /dev/input */
void input_close(void);
/* Waits for a key, at most timeout_ms (-1 = forever). raw_code gets the Linux key code. */
Key input_wait(int timeout_ms, int *raw_code);
const char *key_name(Key k);

/* ------------------------------------------------------------------ font */

typedef enum {
    FACE_REGULAR = 0, FACE_ITALIC = 1, FACE_BOLD = 2, FACE_BOLD_ITALIC = 3, /* Literata: reading, titles */
    FACE_MONO = 4, FACE_MONO_MEDIUM = 5,                                   /* IBM Plex Mono: interface */
    FACE_SANS = 6, FACE_SANS_MEDIUM = 7,                                   /* IBM Plex Sans: menus */
    FACE_SEMIBOLD = 8,                                                     /* Literata: cover titles */
    FACE_MONO_BOLD = 9,                                                    /* covers (Einaudi, Feltrinelli) */
    FACE_BODONI = 10,                                                      /* Libre Bodoni: Sellerio cover */
    FACE_BASK = 11, FACE_BASK_ITALIC = 12,                                 /* Libre Baskerville: covers */
    FACE_COUNT
} Face;

int font_load(const char *dir); /* loads Literata-*.ttf from dir */
/* Advance of a code point in 1/64 px, including kerning with the previous code point (0 = none). */
int font_advance(Face f, int size, uint32_t prev, uint32_t cp);
/* Draws one code point with its origin on the baseline at (x/64, y). */
void font_draw(Canvas *c, Face f, int size, int x64, int baseline, uint32_t cp, uint8_t ink);
int font_ascent(Face f, int size);
int font_descent(Face f, int size); /* positive */
/* Width of a UTF-8 string in pixels. */
int font_text_width(Face f, int size, const char *s);
/* Draws a UTF-8 string; returns the x after it. Truncates with "…" past max_w (if > 0). */
int font_draw_text(Canvas *c, Face f, int size, int x, int baseline, const char *s, uint8_t ink, int max_w);

/* -------------------------------------------------------------------- ui */

extern int g_sim; /* simulator: fixed clock (21:40) and battery (100%), as in the Figma frames */

typedef enum { ICON_NONE, ICON_ARROW, ICON_PLUS } MenuIcon;

int ui_baseline(Face f, int size, float top, float lh);
void ui_clock(char *out, size_t n);
/* Top bar (48 px). center: small text in the middle or NULL; bar: 0..1 progress on the bottom edge, <0 none. */
void ui_header(Canvas *c, const char *left, const char *center, double bar);
/* Reading footer anchored at y_bottom: texts (any can be NULL) and a 3 px progress bar. */
void ui_progress(Canvas *c, int x, int w, int y_bottom, const char *left, const char *center, const char *right,
                 double frac, uint8_t track);
void ui_menu_row(Canvas *c, int x, int y, int w, const char *label, const char *value, MenuIcon icon, int focus);
void ui_title(Canvas *c, int y, const char *title);
void ui_group_label(Canvas *c, int x, float top, const char *label);
/* Wrapped text, at most max_lines (the last one ends with "…"). Returns the number of lines. */
int ui_text_block(Canvas *c, Face f, int size, float lh, int x, float top, int w, int max_lines, const char *s,
                  uint8_t ink, int count_only);
int cover_model_for(const char *title, const char *author, const char *publisher); /* 0..5 */
void cover_describe(const char *title, const char *author, const char *publisher, char *out, size_t n);
void cover_draw(Canvas *c, float x, float y, float w, float h, const char *title, const char *author,
                const char *publisher, float border);

/* ------------------------------------------------------------------ book */

/* Per-byte style flags of chapter text. */
#define ST_ITALIC 1
#define ST_BOLD 2

typedef enum {
    BLK_PARA,     /* normal paragraph, justified, first-line indent */
    BLK_PARA_NOINDENT,
    BLK_CENTER,   /* centred text */
    BLK_HEADING1, /* chapter title */
    BLK_HEADING2,
    BLK_SEPARATOR /* scene break / hr */
} BlockKind;

typedef struct {
    uint32_t start, end; /* byte range in Chapter.text */
    uint8_t kind;
} Block;

typedef struct {
    char *text;     /* UTF-8, no newlines */
    uint8_t *style; /* one flag byte per text byte */
    uint32_t len, cap;
    Block *blocks;
    int nblocks, capblocks;
} Chapter;

typedef struct {
    char *title;
    int spine; /* spine index it points to */
} TocEntry;

typedef struct Book {
    char *path;
    char *title, *author, *language;
    int is_txt;
    int nspine;
    char **spine_href;     /* zip entry names */
    uint32_t *spine_size;  /* uncompressed sizes, for the progress percentage */
    uint64_t total_size;
    TocEntry *toc;
    int ntoc;
    void *zip; /* mz_zip_archive* */
} Book;

Book *book_open(const char *path);
void book_close(Book *b);
/* Reads only title/author (fast path for the library). Returns 0 on success. */
/* Reads title, author and publisher (dc:publisher, "" if missing) without opening the chapters. */
int book_peek_meta(const char *path, char **title, char **author, char **publisher, long long *text_bytes);
/* Loads and parses one spine item. Returns NULL on error. */
Chapter *book_chapter(Book *b, int spine);
void chapter_free(Chapter *c);

/* ---------------------------------------------------------------- layout */

typedef struct {
    int width, height;     /* screen */
    int margin_x, margin_top, margin_bottom;
    int size;              /* body text px */
    int line_pct;          /* line height in % of size */
    int justify;
    int indent;            /* first-line indent in px */
    int hyphenate;         /* Italian hyphenation (sillabazione) */
} LayoutParams;

typedef struct {
    int block;
    uint32_t pos; /* byte offset in chapter text */
} TextPos;

typedef struct {
    TextPos *starts;
    int n, cap;
} Pagination;

/* Lays out one page from 'from'. If c is non-NULL draws it. Returns the start of the next page;
 * next.block == ch->nblocks means end of chapter. */
TextPos layout_page(const LayoutParams *lp, const Chapter *ch, TextPos from, Canvas *c);
void paginate(const LayoutParams *lp, const Chapter *ch, Pagination *out);
int pagination_find(const Pagination *p, uint32_t pos); /* page that contains byte pos */
/* Italian syllables: marks pts[i] = 1 where a word of n code points may be split before letter i. */
void hyphen_points_it(const uint32_t *cps, int n, uint8_t *pts);

/* --------------------------------------------------------------- library */

typedef struct {
    char *path;
    char *title, *author;
    char *publisher; /* dc:publisher, "" if unknown: picks the cover model */
    long long size, mtime;
    /* saved position */
    int spine;
    uint32_t pos;
    int percent;      /* -1 = never opened */
    long long last_read;
    long long text_bytes; /* size of the text (uncompressed XHTML), to estimate pages */
    int pages;            /* estimated pages, 0 = unknown */
} LibEntry;

typedef struct {
    LibEntry *e;
    int n, cap;
} Library;

void library_scan(Library *lib, const char *books_dir, const char *data_dir);
void library_free(Library *lib);
void library_save_positions(const Library *lib, const char *data_dir);
LibEntry *library_find(Library *lib, const char *path);
void reading_log(const char *data_dir, const char *path, long long start, long long end, int pages);

#endif
