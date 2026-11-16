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

#define CARBONELLA_VERSION "0.1.1"

/* ------------------------------------------------------------------ util */

extern volatile int g_quit; /* set by SIGTERM/SIGINT */

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
#define INK_PAPER 0
#define INK_BLACK 255
#define INK_DARK 200 /* secondary text: still dark enough for e-ink */
#define INK_MID 140  /* separators */

typedef struct {
    int w, h;
    uint8_t *px;
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
    K_QUIT,         /* end of simulator script */
} Key;

int input_open(const char *sim_script); /* sim_script NULL = /dev/input */
void input_close(void);
/* Waits for a key, at most timeout_ms (-1 = forever). raw_code gets the Linux key code. */
Key input_wait(int timeout_ms, int *raw_code);
const char *key_name(Key k);

/* ------------------------------------------------------------------ font */

typedef enum { FACE_REGULAR = 0, FACE_ITALIC = 1, FACE_BOLD = 2, FACE_BOLD_ITALIC = 3, FACE_COUNT } Face;

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
int book_peek_meta(const char *path, char **title, char **author);
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
    long long size, mtime;
    /* saved position */
    int spine;
    uint32_t pos;
    int percent;      /* -1 = never opened */
    long long last_read;
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
