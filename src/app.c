/*
 * Carbonella - screens and main loop, drawn as in the Figma design.
 *
 *   Home      3 columns of covers (generated per book), scrolls by rows; the first cell holds the
 *             books already read ("Libri letti"), sub-folders of documents become collections (piles)
 *   Lettura   the page, with "12 di 684" and the progress bar of the whole book at the bottom
 *   Menu      a panel from the bottom (Home and Lettura), as in the "Pannello menu" component
 *   Standby   when the Kindle goes to sleep, the cover of the book full screen
 *
 * Keys (Kindle 4):
 *   5-way                          move between covers / menu rows; turn pages while reading
 *   page keys                      turn pages; in the Home scroll by two rows
 *   centre                         open / choose
 *   Menu                           menu
 *   Back                           go back one level
 *   Home                           Home; from the Home: back to the Kindle
 */
#include "carbonella.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

volatile int g_quit;
volatile int g_power;

static const int SIZES[] = { 14, 16, 18, 20, 22, 24, 28 };
#define NSIZES ((int)(sizeof SIZES / sizeof SIZES[0]))
static const int GOALS[] = { 10, 15, 20, 30, 45, 60, 90 };
#define NGOALS ((int)(sizeof GOALS / sizeof GOALS[0]))
static const char *SORTS[] = { "Più recente", "Titolo", "Autore" };
static const int LINES[] = { 130, 140, 150, 160, 180 };
#define NLINES ((int)(sizeof LINES / sizeof LINES[0]))
static const int MARGINS[] = { 32, 48, 64 };
static const char *MARGIN_NAMES[] = { "Stretti", "Medi", "Larghi" };
static const int FLASHES[] = { 1, 3, 6, 10, 20 };
#define NFLASHES ((int)(sizeof FLASHES / sizeof FLASHES[0]))
static const int YEARS[] = { 6, 12, 18, 24, 36, 52 };
#define NYEARS ((int)(sizeof YEARS / sizeof YEARS[0]))

/* Bytes of XHTML per page at 18 px, to estimate pages before a book is opened. */
#define BYTES_PER_PAGE_18 2300.0

typedef struct {
    Settings cfg;
    char base[512], data[600], books[512], prefs_path[700];
    char name[128];
    Canvas cv;
    int W, H;
    Library lib;
    LayoutParams lp;
    int flash_every, idle_ms, goal_min, sort, books_year;
    int standby_seen; /* the Kindle told us about standby: no need for our own idle exit */
    /* reader */
    Book *book;
    LibEntry *entry;
    int spine;
    Chapter *ch;
    Pagination pg;
    int page;
    int turns, unsaved;
    long long session_start;
    int session_pages;
    double cal_bytes, cal_pages; /* calibration of bytes per page, from the chapters paginated */
    int cal_last_spine;
} App;

typedef enum { GO_LIBRARY, GO_READER, GO_EXIT } Next;

static LibEntry *g_selected;

/* ------------------------------------------------------------ helpers */

static void layout_from_settings(App *a)
{
    int size = settings_int(&a->cfg, "dimensione_testo", 18);
    int margin = settings_int(&a->cfg, "margine", 48);
    a->lp = (LayoutParams){
        .width = a->W,
        .height = a->H,
        .margin_x = margin,
        .margin_top = 48,
        .margin_bottom = 48,
        .size = size,
        .line_pct = settings_int(&a->cfg, "interlinea", 150),
        .justify = settings_int(&a->cfg, "giustifica", 1),
        .indent = size * 4 / 3,
    };
}

static void free_settings(Settings *p)
{
    for (int i = 0; i < p->n; i++) {
        free(p->key[i]);
        free(p->val[i]);
    }
}

static void save_prefs(App *a)
{
    Settings p = { 0 };
    settings_load(&p, a->prefs_path); /* keep whatever else the user wrote there */
    settings_set_int(&p, "dimensione_testo", a->lp.size);
    settings_set_int(&p, "obiettivo_minuti", a->goal_min);
    settings_set_int(&p, "ordina", a->sort);
    settings_set_int(&p, "interlinea", a->lp.line_pct);
    settings_set_int(&p, "margine", a->lp.margin_x);
    settings_set_int(&p, "lampeggio_ogni", a->flash_every);
    settings_set_int(&p, "libri_anno", a->books_year);
    settings_save(&p, a->prefs_path, "# Preferenze personali: valgono più di carbonella.conf e restano dopo gli aggiornamenti.");
    free_settings(&p);
}

static void full(App *a, RefreshKind kind)
{
    display_update(&a->cv, (Rect){ 0, 0, a->W, a->H }, kind);
}

/* Minutes read today (sessions in letture.tsv, plus the one in progress). */
static int minutes_today(App *a)
{
    if (g_sim)
        return 13; /* as in the Figma frame */
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    long long midnight = mktime(&tm);
    long long secs = 0;
    char path[700];
    snprintf(path, sizeof path, "%s/letture.tsv", a->data);
    char *s = read_file(path, NULL);
    if (s) {
        for (char *p = s; *p;) {
            long long st = 0, en = 0;
            if (sscanf(p, "%lld\t%lld", &st, &en) == 2 && en > midnight) {
                long long d = en - (st > midnight ? st : midnight);
                secs += d > 0 && d < 4 * 3600 ? d : 0;
            }
            char *nl = strchr(p, '\n');
            if (!nl)
                break;
            p = nl + 1;
        }
        free(s);
    }
    if (a->book && a->session_start)
        secs += now - (a->session_start > midnight ? a->session_start : midnight);
    return (int)(secs / 60);
}

static void home_header(App *a, const char *left)
{
    char today[48];
    int m = minutes_today(a);
    snprintf(today, sizeof today, "Oggi %d/%d minuti", m, a->goal_min);
    ui_header(&a->cv, left ? left : a->name, today, a->goal_min > 0 ? (double)m / a->goal_min : -1);
}

/* A centred box for short messages ("Apro il libro…"). */
static void message(App *a, const char *title, const char *line2, RefreshKind kind)
{
    int w = 456, h = line2 ? 128 : 96;
    int x = (a->W - w) / 2, y = (a->H - h) / 2;
    canvas_fill(&a->cv, x, y, w, h, G_CARTA);
    canvas_frame(&a->cv, x, y, w, h, 3, G_NERO);
    int tw = font_text_width(FACE_SANS_MEDIUM, 22, title);
    if (tw > w - 48)
        tw = w - 48;
    font_draw_text(&a->cv, FACE_SANS_MEDIUM, 22, x + (w - tw) / 2, y + 56, title, G_NERO, w - 48);
    if (line2) {
        int lw = font_text_width(FACE_MONO, 14, line2);
        if (lw > w - 48)
            lw = w - 48;
        font_draw_text(&a->cv, FACE_MONO, 14, x + (w - lw) / 2, y + 94, line2, G_GRAFITE, w - 48);
    }
    display_update(&a->cv, (Rect){ x, y, w, h }, kind);
}

/* The generated cover depends on title and author, so it stays the same if the file moves. */
static int book_style(const LibEntry *e)
{
    char key[600];
    snprintf(key, sizeof key, "%s|%s", e->title ? e->title : "", e->author ? e->author : "");
    return cover_style_for(key);
}

/* ------------------------------------------------------------ standby */

/* The Kindle is going to sleep: the cover of the book full screen (Simone's design); when it
 * wakes up, the screen comes back as it was. */
static void standby(App *a)
{
    a->standby_seen = 1;
    uint8_t *saved = xmalloc((size_t)a->W * a->H);
    memcpy(saved, a->cv.px, (size_t)a->W * a->H);
    LibEntry *e = a->entry && a->book ? a->entry : (a->lib.n > 0 ? &a->lib.e[0] : NULL);
    canvas_clear(&a->cv, G_CARTA);
    if (e)
        cover_draw(&a->cv, 0, 0, a->W, a->H, e->title, e->author, book_style(e), 0);
    else
        cover_draw(&a->cv, 0, 0, a->W, a->H, "Carbonella", a->name, 0, 0);
    full(a, REFRESH_FULL);
    log_msg("standby");
    for (;;) {
        Key k = input_wait(-1, NULL);
        if (k == K_WAKE || k == K_QUIT)
            break;
    }
    log_msg("wake up");
    memcpy(a->cv.px, saved, (size_t)a->W * a->H);
    free(saved);
    full(a, REFRESH_FULL);
}

/* input_wait that also handles standby. After a standby it returns K_NONE (nothing to do). */
static Key wait_key(App *a, int *raw)
{
    Key k = input_wait(a->standby_seen ? -1 : a->idle_ms, raw);
    if (k == K_SLEEP) {
        standby(a);
        return K_NONE;
    }
    if (k == K_WAKE)
        return K_NONE;
    return k;
}

/* ----------------------------------------------------------- menu panel */

typedef struct {
    const char *label;
    char value[48];
    MenuIcon icon;
} Row;

#define PANEL_END -100

/* "Pannello menu": rows over the bottom of the screen, focus = black row.
 * Returns the chosen row; -1 closed; -2 (+ *key) for Left/Right on the 'stepper' row;
 * PANEL_END (+ *key) for Home, timeout, quit. */
static int panel_run(App *a, Row *rows, int n, int sel, const char *footer_l, const char *footer_c,
                     const char *footer_r, double frac, int stepper, Key *key)
{
    int h = 1 + 16 + n * 56 + 16 + 48;
    int y0 = a->H - h;
    for (;;) {
        canvas_fill(&a->cv, 0, y0, a->W, h, G_CARTA);
        canvas_fill(&a->cv, 0, y0, a->W, 1, G_CENERE);
        for (int i = 0; i < n; i++) {
            char val[64];
            const char *v = rows[i].value;
            if (i == sel && i == stepper) {
                snprintf(val, sizeof val, "\xE2\x80\xB9  %s  \xE2\x80\xBA", rows[i].value);
                v = val;
            }
            ui_menu_row(&a->cv, 16, y0 + 17 + i * 56, a->W - 32, rows[i].label, v,
                        i == stepper ? ICON_NONE : rows[i].icon, i == sel);
        }
        if (footer_l || footer_r)
            ui_progress(&a->cv, 48, a->W - 96, a->H - 8, footer_l, NULL, footer_r, frac, G_SMOG);
        else if (footer_c) {
            int tw = font_text_width(FACE_MONO, 14, footer_c);
            font_draw_text(&a->cv, FACE_MONO, 14, (a->W - tw) / 2,
                           ui_baseline(FACE_MONO, 14, a->H - 8 - 16.8f, 16.8f), footer_c, G_GRAFITE, 0);
        }
        display_update(&a->cv, (Rect){ 0, y0, a->W, h }, REFRESH_PARTIAL);
        for (;;) {
            int raw;
            Key k = wait_key(a, &raw);
            if (k == K_UP) {
                sel = sel > 0 ? sel - 1 : n - 1;
                break;
            }
            if (k == K_DOWN) {
                sel = sel < n - 1 ? sel + 1 : 0;
                break;
            }
            if (k == K_SELECT)
                return sel;
            if (k == K_BACK || k == K_MENU)
                return -1;
            if ((k == K_LEFT || k == K_RIGHT) && sel == stepper) {
                *key = k;
                return -2;
            }
            if (k == K_HOME || k == K_TIMEOUT || k == K_QUIT) {
                *key = k;
                return PANEL_END;
            }
        }
    }
}

/* ----------------------------------------------------------- list screen */

typedef struct {
    const char *label;
    const char *value;
} ListItem;

#define LIST_TOP 136
#define LIST_ROW 56

/* Full-screen list (Indice, confirmations), styled as the "04 · Impostazioni" frame.
 * Returns the chosen index, -1 for Back, -2 for another key (in *key). */
static int list_run(App *a, const char *header, const char *title, const ListItem *items, int n, int sel, Key *key)
{
    int per = (a->H - 48 - LIST_TOP) / LIST_ROW;
    if (sel >= n)
        sel = n - 1;
    if (sel < 0)
        sel = 0;
    int shown_page = -1;
    for (;;) {
        int page = sel / per, first = page * per;
        if (page != shown_page) {
            canvas_clear(&a->cv, G_CARTA);
            ui_header(&a->cv, header, NULL, -1);
            ui_title(&a->cv, 48, title);
            for (int i = 0; i < per && first + i < n; i++)
                ui_menu_row(&a->cv, 32, LIST_TOP + i * LIST_ROW, a->W - 64, items[first + i].label,
                            items[first + i].value, ICON_ARROW, first + i == sel);
            if (n > per) {
                char pg[48];
                snprintf(pg, sizeof pg, "Pagina %d di %d", page + 1, (n + per - 1) / per);
                int tw = font_text_width(FACE_MONO, 16, pg);
                font_draw_text(&a->cv, FACE_MONO, 16, (a->W - tw) / 2,
                               ui_baseline(FACE_MONO, 16, a->H - 8 - 19.2f, 19.2f), pg, G_CENERE, 0);
            }
            full(a, shown_page < 0 ? REFRESH_FULL : REFRESH_PARTIAL);
            shown_page = page;
        }
        int raw, old = sel;
        Key k = wait_key(a, &raw);
        switch (k) {
        case K_UP: sel = sel > 0 ? sel - 1 : n - 1; break;
        case K_DOWN: sel = sel < n - 1 ? sel + 1 : 0; break;
        case K_LEFT:
        case K_PREV: sel = sel - per >= 0 ? (sel / per - 1) * per : sel; break;
        case K_RIGHT:
        case K_NEXT: sel = (sel / per + 1) * per < n ? (sel / per + 1) * per : sel; break;
        case K_SELECT: return sel;
        case K_BACK: return -1;
        case K_NONE: break;
        default:
            if (key)
                *key = k;
            return -2;
        }
        if (sel != old && sel / per == old / per) {
            int yo = LIST_TOP + (old % per) * LIST_ROW, ys = LIST_TOP + (sel % per) * LIST_ROW;
            ui_menu_row(&a->cv, 32, yo, a->W - 64, items[old].label, items[old].value, ICON_ARROW, 0);
            ui_menu_row(&a->cv, 32, ys, a->W - 64, items[sel].label, items[sel].value, ICON_ARROW, 1);
            display_update(&a->cv, (Rect){ 32, yo, a->W - 64, LIST_ROW }, REFRESH_PARTIAL);
            display_update(&a->cv, (Rect){ 32, ys, a->W - 64, LIST_ROW }, REFRESH_PARTIAL);
        }
    }
}

static int confirm_exit(App *a)
{
    ListItem items[] = { { "No, resta in Carbonella", NULL }, { "Sì, torna al Kindle", NULL } };
    Key k;
    int r = list_run(a, a->name, "Tornare al Kindle?", items, 2, 0, &k);
    return r == 1 || (r == -2 && (k == K_TIMEOUT || k == K_QUIT));
}

/* ------------------------------------------------------------- reader */

static void close_book(App *a)
{
    chapter_free(a->ch);
    a->ch = NULL;
    book_close(a->book);
    a->book = NULL;
    a->pg.n = 0;
}

static int percent_of(App *a)
{
    if (!a->book || a->book->total_size == 0)
        return 0;
    uint64_t done = 0;
    for (int i = 0; i < a->spine; i++)
        done += a->book->spine_size[i];
    double frac = a->pg.n > 0 ? (double)a->page / a->pg.n : 0.0;
    if (a->spine == a->book->nspine - 1 && a->page == a->pg.n - 1)
        return 100;
    double p = (done + frac * a->book->spine_size[a->spine]) * 100.0 / a->book->total_size;
    return p > 99 ? 99 : (int)p;
}

static double default_bpp(int size)
{
    return BYTES_PER_PAGE_18 * (size / 18.0) * (size / 18.0);
}

static double bytes_per_page(App *a)
{
    return a->cal_pages >= 4 ? a->cal_bytes / a->cal_pages : default_bpp(a->lp.size);
}

/* Page in the whole book and total pages: exact for the chapter on screen, estimated from the
 * text size for the others. */
static void book_pages(App *a, int *page, int *total)
{
    double bpp = bytes_per_page(a);
    uint64_t before = 0, after = 0;
    for (int i = 0; i < a->spine; i++)
        before += a->book->spine_size[i];
    for (int i = a->spine + 1; i < a->book->nspine; i++)
        after += a->book->spine_size[i];
    int p = (int)(before / bpp + 0.5) + a->page + 1;
    int t = p + (a->pg.n - a->page - 1) + (int)(after / bpp + 0.5);
    *page = p;
    *total = t < p ? p : t;
}

static void save_position(App *a)
{
    if (!a->entry || !a->ch || a->pg.n == 0)
        return;
    a->entry->spine = a->spine;
    a->entry->pos = a->pg.starts[a->page].pos;
    a->entry->percent = percent_of(a);
    a->entry->last_read = time(NULL);
    int p, t;
    book_pages(a, &p, &t);
    a->entry->pages = t;
    library_save_positions(&a->lib, a->data);
    a->unsaved = 0;
}

/* Loads a chapter. which_page: 0 first, -1 last, or keeps the page containing byte 'pos'. */
static int load_chapter(App *a, int spine, int which_page, uint32_t pos)
{
    Chapter *c = book_chapter(a->book, spine);
    if (!c)
        return -1;
    chapter_free(a->ch);
    a->ch = c;
    a->spine = spine;
    double t0 = now_ms();
    paginate(&a->lp, a->ch, &a->pg);
    log_msg("paginated chapter %d: %d pages in %.0f ms", spine, a->pg.n, now_ms() - t0);
    if (spine != a->cal_last_spine && a->pg.n >= 2 && a->book->spine_size[spine] > 0) {
        a->cal_bytes += a->book->spine_size[spine];
        a->cal_pages += a->pg.n;
        a->cal_last_spine = spine;
    }
    if (which_page == 0)
        a->page = 0;
    else if (which_page == -1)
        a->page = a->pg.n - 1;
    else
        a->page = pagination_find(&a->pg, pos);
    return 0;
}

static void draw_page(App *a)
{
    canvas_clear(&a->cv, G_CARTA);
    if (a->ch && a->pg.n > 0)
        layout_page(&a->lp, a->ch, a->pg.starts[a->page], &a->cv);
    if (a->ch && a->ch->nblocks == 0)
        font_draw_text(&a->cv, FACE_ITALIC, 18, a->lp.margin_x, a->H / 2, "(pagina senza testo)", G_GRAFITE, 0);
    int p, t;
    book_pages(a, &p, &t);
    char pos[48];
    snprintf(pos, sizeof pos, "%d di %d", p, t);
    ui_progress(&a->cv, a->lp.margin_x, a->W - 2 * a->lp.margin_x, a->H, NULL, pos, NULL, percent_of(a) / 100.0,
                G_VELO);
}

static void render_page(App *a, RefreshKind kind)
{
    draw_page(a);
    full(a, kind);
}

static void turn(App *a, int dir)
{
    int ok = 1;
    RefreshKind kind = REFRESH_PARTIAL;
    if (dir > 0) {
        if (a->page + 1 < a->pg.n)
            a->page++;
        else if (a->spine + 1 < a->book->nspine) {
            ok = load_chapter(a, a->spine + 1, 0, 0) == 0;
            kind = REFRESH_FULL;
        } else
            ok = 0;
    } else {
        if (a->page > 0)
            a->page--;
        else if (a->spine > 0) {
            ok = load_chapter(a, a->spine - 1, -1, 0) == 0;
            kind = REFRESH_FULL;
        } else
            ok = 0;
    }
    if (!ok)
        return;
    a->session_pages++;
    if (++a->turns >= a->flash_every)
        kind = REFRESH_FULL;
    if (kind == REFRESH_FULL)
        a->turns = 0;
    render_page(a, kind);
    if (++a->unsaved >= 5)
        save_position(a);
}

static void show_toc(App *a)
{
    int n = a->book->ntoc > 0 ? a->book->ntoc : a->book->nspine;
    ListItem *items = xcalloc(n, sizeof(ListItem));
    char **labels = xcalloc(n, sizeof(char *));
    int sel = 0;
    for (int i = 0; i < n; i++) {
        if (a->book->ntoc > 0) {
            items[i].label = a->book->toc[i].title;
            if (a->book->toc[i].spine < a->spine || (a->book->toc[i].spine == a->spine &&
                                                       (i == 0 || a->book->toc[i - 1].spine != a->spine)))
                sel = i;
        } else {
            labels[i] = xmalloc(32);
            snprintf(labels[i], 32, "Sezione %d", i + 1);
            items[i].label = labels[i];
            if (i == a->spine)
                sel = i;
        }
    }
    int r = list_run(a, a->book->title, "Indice", items, n, sel, NULL);
    if (r >= 0) {
        int spine = a->book->ntoc > 0 ? a->book->toc[r].spine : r;
        load_chapter(a, spine, 0, 0);
    }
    for (int i = 0; i < n; i++)
        free(labels[i]);
    free(labels);
    free(items);
}

static int size_index(int size)
{
    int best = 0;
    for (int i = 0; i < NSIZES; i++)
        if (abs(SIZES[i] - size) < abs(SIZES[best] - size))
            best = i;
    return best;
}

/* Paginate the open chapter again after a change of size, line spacing or margins,
 * staying on the same text. */
static void relayout(App *a)
{
    a->lp.indent = a->lp.size * 4 / 3;
    if (!a->book || !a->ch)
        return;
    uint32_t pos = a->pg.n ? a->pg.starts[a->page].pos : 0;
    paginate(&a->lp, a->ch, &a->pg);
    a->page = pagination_find(&a->pg, pos);
    a->cal_bytes = a->cal_pages = 0;
    a->cal_last_spine = -1;
    if (a->pg.n >= 2 && a->book->spine_size[a->spine] > 0) {
        a->cal_bytes = a->book->spine_size[a->spine];
        a->cal_pages = a->pg.n;
        a->cal_last_spine = a->spine;
    }
}

static void change_size(App *a, int delta)
{
    int i = size_index(a->lp.size) + delta;
    if (i < 0 || i >= NSIZES)
        return;
    a->lp.size = SIZES[i];
    relayout(a);
    save_prefs(a);
}

/* index of v in a list of n values (the closest one) */
static int closest(const int *list, int n, int v)
{
    int best = 0;
    for (int i = 0; i < n; i++)
        if (abs(list[i] - v) < abs(list[best] - v))
            best = i;
    return best;
}

static int step(const int *list, int n, int v, int dir)
{
    return list[(closest(list, n, v) + dir + n) % n];
}

/* ------------------------------------------------------------ settings */

static void key_test(App *a);

enum {
    S_GOAL, S_YEAR, S_FONT, S_SIZE, S_LINE, S_MARGIN, S_FLASH, S_VIEW, /* page 1, as in Figma */
    S_SORT, S_RESCAN, S_KEYS, S_VERSION,                               /* page 2 */
    S_N
};

typedef struct {
    int page, group_top, row_top; /* group_top: y of the group label, 0 = same group as before */
    const char *group, *label;
} SetItem;

/* "04 · Impostazioni": header, title, groups (label + rows of 56 px), "Pagina 1 di 2" at the bottom.
 * Rows from the frame: LETTURA label at 149.6, rows from 173; ASPETTO label at 313, rows from 337. */
static const SetItem SET[S_N] = {
    { 0, 150, 173, "Lettura", "Obiettivo giornaliero" },
    { 0, 0, 229, NULL, "Libri da leggere quest'anno" },
    { 0, 313, 337, "Aspetto", "Carattere" },
    { 0, 0, 393, NULL, "Dimensione" },
    { 0, 0, 449, NULL, "Interlinea" },
    { 0, 0, 505, NULL, "Margini" },
    { 0, 0, 561, NULL, "Refresh completo" },
    { 0, 0, 617, NULL, "Visualizzazione" },
    { 1, 150, 173, "Libreria", "Ordina per" },
    { 1, 0, 229, NULL, "Aggiorna l'elenco dei libri" },
    { 1, 313, 337, "Carbonella", "Prova dei tasti" },
    { 1, 0, 393, NULL, "Versione" },
};

static void set_value(App *a, int i, char *out, size_t n)
{
    out[0] = 0;
    switch (i) {
    case S_GOAL: snprintf(out, n, "%d minuti", a->goal_min); break;
    case S_YEAR: snprintf(out, n, "%d", a->books_year); break;
    case S_FONT: snprintf(out, n, "Literata"); break;
    case S_SIZE: snprintf(out, n, "%d pt", a->lp.size); break;
    case S_LINE: snprintf(out, n, "%d,%d", a->lp.line_pct / 100, a->lp.line_pct % 100 / 10); break;
    case S_MARGIN: snprintf(out, n, "%s", MARGIN_NAMES[closest(MARGINS, 3, a->lp.margin_x)]); break;
    case S_FLASH:
        if (a->flash_every == 1)
            snprintf(out, n, "ogni pagina");
        else
            snprintf(out, n, "ogni %d pagine", a->flash_every);
        break;
    case S_VIEW: snprintf(out, n, "Copertine"); break;
    case S_SORT: snprintf(out, n, "%s", SORTS[a->sort]); break;
    case S_VERSION: snprintf(out, n, "%s", CARBONELLA_VERSION); break;
    default: break;
    }
}

static void set_row(App *a, int i, int focus)
{
    char v[48];
    set_value(a, i, v, sizeof v);
    ui_menu_row(&a->cv, 32, SET[i].row_top, a->W - 64, SET[i].label, v,
                i == S_RESCAN ? ICON_PLUS : i == S_VERSION ? ICON_NONE : ICON_ARROW, focus);
}

/* Change a setting by one step (dir +1 / -1). Returns 1 if the reading page must be laid out again. */
static int set_change(App *a, int i, int dir)
{
    switch (i) {
    case S_GOAL: a->goal_min = step(GOALS, NGOALS, a->goal_min, dir); break;
    case S_YEAR: a->books_year = step(YEARS, NYEARS, a->books_year, dir); break;
    case S_SIZE: a->lp.size = step(SIZES, NSIZES, a->lp.size, dir); break;
    case S_LINE: a->lp.line_pct = step(LINES, NLINES, a->lp.line_pct, dir); break;
    case S_MARGIN: a->lp.margin_x = step(MARGINS, 3, a->lp.margin_x, dir); break;
    case S_FLASH: a->flash_every = step(FLASHES, NFLASHES, a->flash_every, dir); break;
    case S_SORT: a->sort = (a->sort + (dir > 0 ? 1 : 2)) % 3; break;
    default: return 0; /* Carattere and Visualizzazione have one choice for now */
    }
    save_prefs(a);
    return i == S_SIZE || i == S_LINE || i == S_MARGIN;
}

static void settings_draw(App *a, int page, int sel)
{
    canvas_clear(&a->cv, G_CARTA);
    home_header(a, NULL);
    ui_title(&a->cv, 48, "Impostazioni");
    for (int i = 0; i < S_N; i++) {
        if (SET[i].page != page)
            continue;
        if (SET[i].group)
            ui_group_label(&a->cv, 52, SET[i].group_top, SET[i].group);
        set_row(a, i, i == sel);
    }
    char pg[32];
    snprintf(pg, sizeof pg, "Pagina %d di 2", page + 1);
    int tw = font_text_width(FACE_MONO, 16, pg);
    font_draw_text(&a->cv, FACE_MONO, 16, (a->W - tw) / 2, ui_baseline(FACE_MONO, 16, a->H - 8 - 19.2f, 19.2f), pg,
                   G_CENERE, 0);
}

/* The settings screen. Up/down move, centre and right go to the next value, left to the previous one,
 * Back closes. Returns 1 to rescan the books, 2 to go back to the Kindle (timeout), 0 otherwise. */
static int settings_run(App *a)
{
    int sel = 0, shown = -1, relaid = 0, result = 0;
    for (;;) {
        int page = SET[sel].page;
        if (page != shown) {
            settings_draw(a, page, sel);
            full(a, shown < 0 ? REFRESH_FULL : REFRESH_PARTIAL);
            shown = page;
        }
        int raw, old = sel;
        Key k = wait_key(a, &raw);
        if (k == K_NONE) { /* back from standby: the screen is already restored */
            continue;
        } else if (k == K_UP) {
            sel = (sel + S_N - 1) % S_N;
        } else if (k == K_DOWN) {
            sel = (sel + 1) % S_N;
        } else if (k == K_PREV) {
            sel = page == 0 ? S_SORT : 0;
        } else if (k == K_NEXT) {
            sel = page == 0 ? S_SORT : 0;
        } else if (k == K_SELECT || k == K_RIGHT || k == K_LEFT) {
            if (sel == S_RESCAN && k == K_SELECT) {
                result = 1;
                break;
            }
            if (sel == S_KEYS && k == K_SELECT) {
                key_test(a);
                shown = -1;
                continue;
            }
            relaid |= set_change(a, sel, k == K_LEFT ? -1 : 1);
            if (sel == S_GOAL) { /* the bar in the header changes too */
                settings_draw(a, page, sel);
                full(a, REFRESH_PARTIAL);
                continue;
            }
            set_row(a, sel, 1);
            display_update(&a->cv, (Rect){ 32, SET[sel].row_top, a->W - 64, 56 }, REFRESH_PARTIAL);
            continue;
        } else if (k == K_BACK || k == K_MENU || k == K_HOME) {
            break;
        } else if (k == K_TIMEOUT || k == K_QUIT) {
            result = 2;
            break;
        } else {
            continue;
        }
        if (SET[sel].page == SET[old].page) {
            set_row(a, old, 0);
            set_row(a, sel, 1);
            display_update(&a->cv, (Rect){ 32, SET[old].row_top, a->W - 64, 56 }, REFRESH_PARTIAL);
            display_update(&a->cv, (Rect){ 32, SET[sel].row_top, a->W - 64, 56 }, REFRESH_PARTIAL);
        }
    }
    if (relaid)
        relayout(a);
    return result;
}

/* "02 · Lettura Menù": the panel opens over the page, which stays visible above it (Simone, v0.3);
 * the header shows the book title. Returns where to go after the menu, or -1 to keep reading. */
static int reader_menu(App *a)
{
    int sel = 0;
    enum { M_TOC, M_SIZE, M_SETTINGS, M_CLEAN, M_LIB, M_KINDLE, M_N };
    Row rows[M_N] = {
        { "Indice", "", ICON_ARROW },
        { "Dimensione carattere", "", ICON_NONE },
        { "Impostazioni", "", ICON_ARROW },
        { "Pulisci lo schermo", "", ICON_NONE },
        { "Torna alla Home", "", ICON_ARROW },
        { "Torna al Kindle", "", ICON_ARROW },
    };
    int redraw = 1;
    for (;;) {
        if (redraw) { /* the page under the panel, with the title in the header */
            draw_page(a);
            ui_header(&a->cv, a->book->title, NULL, -1);
            full(a, REFRESH_PARTIAL);
            redraw = 0;
        }
        snprintf(rows[M_SIZE].value, sizeof rows[M_SIZE].value, "%d pt", a->lp.size);
        int p, t;
        book_pages(a, &p, &t);
        char left[16], right[48];
        snprintf(left, sizeof left, "%d%%", percent_of(a));
        snprintf(right, sizeof right, "pagina %d di %d", p, t);
        Key k = K_NONE;
        int r = panel_run(a, rows, M_N, sel, left, NULL, right, percent_of(a) / 100.0, M_SIZE, &k);
        switch (r) {
        case -2: /* left/right on the size row */
            change_size(a, k == K_RIGHT ? 1 : -1);
            sel = M_SIZE;
            redraw = 1;
            continue;
        case M_TOC: show_toc(a); return -1;
        case M_SIZE: /* centre: next size, after the biggest back to the smallest */
            change_size(a, size_index(a->lp.size) == NSIZES - 1 ? -(NSIZES - 1) : 1);
            sel = M_SIZE;
            redraw = 1;
            continue;
        case M_SETTINGS: {
            int s = settings_run(a);
            if (s == 2)
                return GO_EXIT;
            if (s == 1)
                return GO_LIBRARY;
            sel = M_SETTINGS;
            redraw = 1;
            continue;
        }
        case M_CLEAN: return -1;
        case M_LIB: return GO_LIBRARY;
        case M_KINDLE: return GO_EXIT;
        case PANEL_END: return k == K_HOME ? GO_LIBRARY : GO_EXIT;
        default: return -1;
        }
    }
}

static Next run_reader(App *a)
{
    message(a, "Apro il libro…", a->entry->title, REFRESH_PARTIAL);
    a->book = book_open(a->entry->path);
    if (!a->book) {
        message(a, "Non riesco ad aprire il libro", "Il file potrebbe essere rovinato o protetto (DRM).",
                REFRESH_PARTIAL);
        input_wait(8000, NULL);
        return GO_LIBRARY;
    }
    /* Italian hyphenation only for Italian books (or books that do not say their language) */
    const char *lang = a->book->language ? a->book->language : "";
    a->lp.hyphenate = settings_int(&a->cfg, "sillabazione", 1) && (lang[0] == 0 || strncasecmp(lang, "it", 2) == 0);
    /* bytes per page: start from the total pages counted last time, refine with each chapter */
    a->cal_bytes = a->cal_pages = 0;
    if (a->entry->pages > 0 && a->book->total_size > 0) {
        a->cal_pages = 8; /* weighs like a short chapter: the real chapters soon count more */
        a->cal_bytes = (double)a->book->total_size / a->entry->pages * a->cal_pages;
    }
    a->cal_last_spine = -1;
    int spine = a->entry->percent >= 0 ? a->entry->spine : 0;
    if (spine >= a->book->nspine)
        spine = 0;
    if (load_chapter(a, spine, 1, a->entry->percent >= 0 ? a->entry->pos : 0) != 0 &&
        load_chapter(a, 0, 0, 0) != 0) {
        close_book(a);
        return GO_LIBRARY;
    }
    /* skip empty chapters (cover pages with only an image) at the very beginning */
    while (a->entry->percent < 0 && a->ch->nblocks == 0 && a->spine + 1 < a->book->nspine)
        load_chapter(a, a->spine + 1, 0, 0);
    a->session_start = time(NULL);
    a->session_pages = 0;
    a->turns = 0;
    render_page(a, REFRESH_FULL);
    Next next = GO_LIBRARY;
    for (;;) {
        int raw;
        Key k = wait_key(a, &raw);
        if (k == K_NEXT || k == K_RIGHT || k == K_DOWN)
            turn(a, +1);
        else if (k == K_PREV || k == K_LEFT || k == K_UP)
            turn(a, -1);
        else if (k == K_MENU || k == K_SELECT) {
            int r = reader_menu(a);
            if (r >= 0) {
                next = (Next)r;
                break;
            }
            a->turns = 0;
            render_page(a, REFRESH_FULL);
        } else if (k == K_BACK || k == K_HOME) {
            next = GO_LIBRARY;
            break;
        } else if (k == K_TIMEOUT || k == K_QUIT) {
            next = GO_EXIT;
            break;
        }
    }
    save_position(a);
    reading_log(a->data, a->entry->path, a->session_start, time(NULL), a->session_pages);
    a->session_start = 0;
    close_book(a);
    return next;
}

/* --------------------------------------------------------------- home */

enum { CELL_BOOK, CELL_FOLDER, CELL_READ };

typedef struct {
    int kind;
    LibEntry *e;       /* book */
    char name[128];    /* folder / "Libri letti" */
    int count;
    LibEntry *pile[3]; /* covers of the pile, front first */
    long long recent;  /* for sorting */
} Cell;

/* Grid geometry from "00 · Home 3 colonne (scorrimento)" */
#define G_X0 19.0f
#define G_Y0 72.0f
#define G_CW 180.667f
#define G_CH 281.849f
#define G_GX 10.0f
#define G_GY 14.0f
#define G_FRAME_H 258.1f

/* sub-folder of the books folder that holds the book ("" = none) */
static void folder_of(App *a, const LibEntry *e, char *out, size_t n)
{
    out[0] = 0;
    size_t bl = strlen(a->books);
    if (strncmp(e->path, a->books, bl) != 0 || e->path[bl] != '/')
        return;
    const char *rel = e->path + bl + 1;
    const char *sl = strchr(rel, '/');
    if (!sl)
        return;
    size_t l = (size_t)(sl - rel) < n - 1 ? (size_t)(sl - rel) : n - 1;
    memcpy(out, rel, l);
    out[l] = 0;
}

static const char *sort_key(const Cell *c, int by_author)
{
    if (c->kind == CELL_BOOK)
        return by_author && c->e->author && *c->e->author ? c->e->author : c->e->title;
    return c->name;
}

static int g_sort_mode;
static int cmp_cells(const void *x, const void *y)
{
    const Cell *a = x, *b = y;
    if (a->kind == CELL_READ || b->kind == CELL_READ)
        return a->kind == CELL_READ ? -1 : b->kind == CELL_READ ? 1 : 0;
    if (g_sort_mode == 0 && a->recent != b->recent)
        return a->recent > b->recent ? -1 : 1;
    return strcasecmp(sort_key(a, g_sort_mode == 2), sort_key(b, g_sort_mode == 2));
}

/* books read come first (most recent first), then the others by date of the file */
static long long recent_of(const LibEntry *e)
{
    return e->last_read ? e->last_read : e->mtime / 1000;
}

static void pile_add(Cell *c, LibEntry *e)
{
    c->count++;
    for (int i = 0; i < 3; i++)
        if (!c->pile[i] || recent_of(e) > recent_of(c->pile[i])) {
            for (int j = 2; j > i; j--)
                c->pile[j] = c->pile[j - 1];
            c->pile[i] = e;
            break;
        }
    if (recent_of(e) > c->recent)
        c->recent = recent_of(e);
}

/* Cells of a view: folder NULL and !read_view = Home; read_view = books already read; else that folder. */
static int build_cells(App *a, const char *folder, int read_view, Cell **out)
{
    Cell *cells = xcalloc(a->lib.n + 2, sizeof(Cell));
    int n = 0;
    Cell read = { .kind = CELL_READ };
    snprintf(read.name, sizeof read.name, "Libri letti");
    for (int i = 0; i < a->lib.n; i++) {
        LibEntry *e = &a->lib.e[i];
        int done = e->percent >= 100;
        char f[128];
        folder_of(a, e, f, sizeof f);
        if (read_view) {
            if (done)
                cells[n++] = (Cell){ .kind = CELL_BOOK, .e = e, .recent = recent_of(e) };
            continue;
        }
        if (folder) {
            if (!done && strcmp(f, folder) == 0)
                cells[n++] = (Cell){ .kind = CELL_BOOK, .e = e, .recent = recent_of(e) };
            continue;
        }
        if (done) {
            pile_add(&read, e);
            continue;
        }
        if (*f) {
            int j;
            for (j = 0; j < n; j++)
                if (cells[j].kind == CELL_FOLDER && strcmp(cells[j].name, f) == 0)
                    break;
            if (j == n) {
                cells[n] = (Cell){ .kind = CELL_FOLDER };
                snprintf(cells[n].name, sizeof cells[n].name, "%s", f);
                n++;
            }
            pile_add(&cells[j], e);
            continue;
        }
        cells[n++] = (Cell){ .kind = CELL_BOOK, .e = e, .recent = recent_of(e) };
    }
    if (read.count)
        cells[n++] = read;
    g_sort_mode = a->sort;
    qsort(cells, n, sizeof(Cell), cmp_cells);
    *out = cells;
    return n;
}

static void cell_pos(int i, int top_row, float *x, float *y)
{
    *x = G_X0 + (i % 3) * (G_CW + G_GX);
    *y = G_Y0 + (i / 3 - top_row) * (G_CH + G_GY);
}

static Rect cell_rect(int i, int top_row)
{
    float x, y;
    cell_pos(i, top_row, &x, &y);
    return (Rect){ (int)x - 1, (int)y - 1, (int)G_CW + 3, (int)G_CH + 3 };
}

/* "Info" row under a cover: Plex Mono Medium 13, left and right */
static void info_row(App *a, float x, float y, const char *left, const char *right, int badge)
{
    float top = y + G_FRAME_H + 2.15f + 4.3f;
    int base = ui_baseline(FACE_MONO_MEDIUM, 13, top, 12.9f);
    int lx = (int)lrintf(x + 5);
    if (badge) {
        int tw = font_text_width(FACE_MONO_MEDIUM, 13, left);
        canvas_fill(&a->cv, lx, (int)lrintf(top - 1.08f - 2.15f), tw + 7, (int)lrintf(12.9f + 4.3f), G_NERO);
        font_draw_text(&a->cv, FACE_MONO_MEDIUM, 13, lx + 3, base, left, G_CARTA, 0);
    } else if (left)
        font_draw_text(&a->cv, FACE_MONO_MEDIUM, 13, lx, base, left, G_NERO, right ? 100 : (int)G_CW - 10);
    if (right) {
        int tw = font_text_width(FACE_MONO_MEDIUM, 13, right);
        font_draw_text(&a->cv, FACE_MONO_MEDIUM, 13, (int)lrintf(x + G_CW - 5 - tw), base, right, G_NERO, 0);
    }
}

static int pages_of(App *a, const LibEntry *e)
{
    if (e->pages > 0)
        return e->pages;
    if (e->text_bytes <= 0)
        return 0;
    int p = (int)(e->text_bytes / default_bpp(a->lp.size) + 0.5);
    return p < 1 ? 1 : p;
}

static void frame_clip(Canvas *cv, float x, float y, int focus)
{
    if (focus)
        canvas_clip(cv, (int)lrintf(x + 3), (int)lrintf(y + 3), (int)lrintf(G_CW) - 6, (int)lrintf(G_FRAME_H) - 6);
    else
        canvas_clip(cv, (int)lrintf(x), (int)lrintf(y), (int)lrintf(G_CW), (int)lrintf(G_FRAME_H));
}

static void draw_cell(App *a, const Cell *c, float x, float y, int focus)
{
    Canvas *cv = &a->cv;
    Rect r = { (int)x - 1, (int)y - 1, (int)G_CW + 3, (int)G_CH + 3 };
    canvas_fill(cv, r.x, r.y, r.w, r.h, G_CARTA);
    /* "Focus = cornice nera di 3 px": the cover moves inside the frame and is cut, the grid stays */
    float d = focus ? 3.2f : 0;
    frame_clip(cv, x, y, focus);
    if (c->kind == CELL_BOOK) {
        cover_draw(cv, x + 4.3f + d, y + 4.3f + d, 172.064f, 249.492f, c->e->title, c->e->author,
                   book_style(c->e), 2);
    } else {
        /* "Raccolte": a pile of three covers, the most recent in front */
        static const float off[3][2] = { { 0, 6.24f }, { 4.3f, 1.94f }, { 8.6f, -2.37f } };
        for (int i = 2; i >= 0; i--) {
            const LibEntry *e = c->pile[i] ? c->pile[i] : c->pile[0];
            float px = x + 4.3f + off[i][0] + d, py = y + 4.3f + off[i][1] + d;
            cover_draw(cv, px, py, 167.762f, 243.255f, e->title, e->author, book_style(e), 1);
            canvas_frame(cv, (int)lrintf(px), (int)lrintf(py), 168, 243, 1, G_NERO);
        }
    }
    canvas_noclip(cv);
    if (focus)
        canvas_frame(cv, (int)lrintf(x), (int)lrintf(y), (int)lrintf(G_CW), (int)lrintf(G_FRAME_H), 3, G_NERO);
    if (c->kind == CELL_BOOK) {
        char left[16], right[32];
        int badge = 0;
        const LibEntry *e = c->e;
        if (e->percent >= 100)
            snprintf(left, sizeof left, "Letto");
        else if (e->percent >= 0)
            snprintf(left, sizeof left, "%d%%", e->percent);
        else {
            badge = !g_sim && time(NULL) - e->mtime < 7 * 86400; /* arrived this week */
            snprintf(left, sizeof left, badge ? "NUOVO" : "Nuovo");
        }
        int p = pages_of(a, e);
        snprintf(right, sizeof right, "%d pagine", p);
        info_row(a, x, y, left, p > 0 ? right : NULL, badge);
    } else {
        char cnt[16];
        snprintf(cnt, sizeof cnt, "%d", c->count);
        info_row(a, x, y, c->name, cnt, 0);
    }
}

static void draw_home(App *a, const char *title, const Cell *cells, int n, int sel, int top_row)
{
    canvas_clear(&a->cv, G_CARTA);
    for (int i = top_row * 3; i < n && i < (top_row + 3) * 3; i++) {
        float x, y;
        cell_pos(i, top_row, &x, &y);
        draw_cell(a, &cells[i], x, y, i == sel);
    }
    home_header(a, title);
}

static void empty_home(App *a)
{
    canvas_clear(&a->cv, G_CARTA);
    home_header(a, NULL);
    ui_title(&a->cv, 48, "Nessun libro, per ora.");
    const char *lines[] = { "Collega il Kindle al computer e copia", "file .epub o .txt nella cartella",
                            "documents.", "", "Premi Home per tornare al Kindle." };
    for (int i = 0; i < 5; i++)
        font_draw_text(&a->cv, FACE_SANS, 18, 32, 170 + i * 30, lines[i], i == 4 ? G_GRAFITE : G_NERO, 0);
    full(a, REFRESH_FULL);
}

/* Home menu ("00 Home Menù"). Returns 1 to rescan, 2 to exit, 0 otherwise. */
static int home_menu(App *a)
{
    enum { H_SORT, H_GOAL, H_SETTINGS, H_RESCAN, H_KINDLE, H_N };
    Row rows[H_N] = {
        { "Ordina per", "", ICON_ARROW },
        { "Obiettivo giornaliero", "", ICON_ARROW },
        { "Impostazioni", "", ICON_ARROW },
        { "Aggiorna l'elenco dei libri", "", ICON_PLUS },
        { "Torna al Kindle", "", ICON_ARROW },
    };
    char ver[48];
    snprintf(ver, sizeof ver, "Carbonella %s", CARBONELLA_VERSION);
    int sel = 0;
    for (;;) {
        snprintf(rows[H_SORT].value, sizeof rows[H_SORT].value, "%s", SORTS[a->sort]);
        snprintf(rows[H_GOAL].value, sizeof rows[H_GOAL].value, "%d minuti", a->goal_min);
        Key k = K_NONE;
        int r = panel_run(a, rows, H_N, sel, NULL, ver, NULL, 0, -1, &k);
        if (r >= 0)
            sel = r;
        switch (r) {
        case H_SORT:
            a->sort = (a->sort + 1) % 3;
            save_prefs(a);
            continue;
        case H_GOAL: {
            int i = 0;
            while (i < NGOALS && GOALS[i] <= a->goal_min)
                i++;
            a->goal_min = GOALS[i % NGOALS];
            save_prefs(a);
            continue;
        }
        case H_SETTINGS: {
            int r2 = settings_run(a);
            return r2; /* 0: the Home is drawn again (order and goal may have changed) */
        }
        case H_RESCAN: return 1;
        case H_KINDLE: return 2;
        case PANEL_END: return k == K_HOME ? 0 : 2;
        default: return 0;
        }
    }
}

/* Index of the book to focus: 'focus' if given, else the one being read, else the first book. */
static int initial_sel(const Cell *cells, int n, const LibEntry *focus)
{
    int sel = -1;
    long long best = -1;
    for (int i = 0; i < n; i++) {
        if (cells[i].kind != CELL_BOOK)
            continue;
        if (focus && cells[i].e == focus)
            return i;
        if (cells[i].e->percent >= 0 && cells[i].e->last_read > best) {
            best = cells[i].e->last_read;
            sel = i;
        }
    }
    if (sel >= 0)
        return sel;
    for (int i = 0; i < n; i++)
        if (cells[i].kind == CELL_BOOK)
            return i;
    return 0;
}

/* One grid view. folder NULL and !read_view = Home. Returns GO_READER (g_selected set),
 * GO_LIBRARY (rescan), GO_EXIT, or -1 = back to the parent view. */
static int grid_view(App *a, const char *folder, int read_view, LibEntry *focus)
{
    Cell *cells;
    int n = build_cells(a, folder, read_view, &cells);
    const char *title = read_view ? "Libri letti" : folder;
    int sel = initial_sel(cells, n, focus);
    int top_row = sel / 3 > 0 ? sel / 3 - 1 : 0;
    int result = -1, redraw = 1, moves = 0;
    for (;;) {
        if (n == 0 && !folder && !read_view) {
            empty_home(a);
            Key k = wait_key(a, NULL);
            if (k == K_HOME || k == K_TIMEOUT || k == K_QUIT) {
                result = GO_EXIT;
                break;
            }
            if (k == K_MENU) {
                int m = home_menu(a);
                if (m) {
                    result = m == 1 ? GO_LIBRARY : GO_EXIT;
                    break;
                }
            }
            continue;
        }
        if (n == 0)
            break;
        if (redraw) {
            draw_home(a, title, cells, n, sel, top_row);
            full(a, redraw == 2 ? REFRESH_PARTIAL : REFRESH_FULL);
            redraw = 0;
        }
        int raw, old = sel;
        Key k = wait_key(a, &raw);
        switch (k) {
        case K_LEFT: sel = sel > 0 ? sel - 1 : sel; break;
        case K_RIGHT: sel = sel < n - 1 ? sel + 1 : sel; break;
        case K_UP: sel = sel >= 3 ? sel - 3 : sel; break;
        case K_DOWN: sel = sel + 3 < n ? sel + 3 : (sel / 3 < (n - 1) / 3 ? n - 1 : sel); break;
        case K_NEXT: sel = sel + 6 < n ? sel + 6 : n - 1; break;
        case K_PREV: sel = sel >= 6 ? sel - 6 : 0; break;
        case K_SELECT: {
            Cell *c = &cells[sel];
            if (c->kind == CELL_BOOK) {
                g_selected = c->e;
                result = GO_READER;
                goto out;
            }
            int r = grid_view(a, c->kind == CELL_FOLDER ? c->name : NULL, c->kind == CELL_READ, NULL);
            if (r != -1) {
                result = r;
                goto out;
            }
            redraw = 1;
            continue;
        }
        case K_BACK:
            if (folder || read_view)
                goto out;
            break;
        case K_HOME:
            if (folder || read_view)
                goto out;
            if (confirm_exit(a)) {
                result = GO_EXIT;
                goto out;
            }
            redraw = 1;
            continue;
        case K_MENU: {
            int m = home_menu(a);
            if (m) {
                result = m == 1 ? GO_LIBRARY : GO_EXIT;
                goto out;
            }
            /* the order may have changed */
            LibEntry *keep = cells[sel].kind == CELL_BOOK ? cells[sel].e : NULL;
            free(cells);
            n = build_cells(a, folder, read_view, &cells);
            sel = initial_sel(cells, n, keep);
            top_row = sel / 3 > 0 ? sel / 3 - 1 : 0;
            redraw = 1;
            continue;
        }
        case K_TIMEOUT:
        case K_QUIT: result = GO_EXIT; goto out;
        default: break;
        }
        if (sel == old)
            continue;
        int row = sel / 3;
        if (row < top_row || row > top_row + 1) {
            top_row = row < top_row ? row : row - 1;
            redraw = 2;
            continue;
        }
        float x, y;
        cell_pos(old, top_row, &x, &y);
        draw_cell(a, &cells[old], x, y, 0);
        cell_pos(sel, top_row, &x, &y);
        draw_cell(a, &cells[sel], x, y, 1);
        if (++moves % 20 == 0) /* clean the ghosts of the frame now and then */
            full(a, REFRESH_FULL);
        else {
            display_update(&a->cv, cell_rect(old, top_row), REFRESH_PARTIAL);
            display_update(&a->cv, cell_rect(sel, top_row), REFRESH_PARTIAL);
        }
    }
out:
    free(cells);
    return result;
}

/* --------------------------------------------------------- key test */

static void key_test(App *a)
{
    int homes = 0;
    char lines[12][64];
    int nl = 0;
    for (;;) {
        canvas_clear(&a->cv, G_CARTA);
        ui_header(&a->cv, a->name, NULL, -1);
        ui_title(&a->cv, 48, "Prova dei tasti");
        font_draw_text(&a->cv, FACE_SANS, 18, 32, 160, "Premi i tasti uno alla volta.", G_NERO, 0);
        font_draw_text(&a->cv, FACE_SANS, 18, 32, 188, "Home tre volte di fila per uscire.", G_NERO, 0);
        for (int i = 0; i < nl; i++)
            font_draw_text(&a->cv, FACE_MONO, 18, 32, 250 + i * 38, lines[i], G_NERO, 0);
        full(a, REFRESH_PARTIAL);
        int raw;
        Key k = input_wait(60000, &raw);
        if (k == K_TIMEOUT || k == K_QUIT)
            return;
        homes = k == K_HOME ? homes + 1 : 0;
        if (homes >= 3)
            return;
        if (nl == 12) {
            memmove(lines, lines + 1, sizeof lines[0] * 11);
            nl = 11;
        }
        snprintf(lines[nl++], 64, "codice %d  →  %s", raw, key_name(k));
        log_msg("key test: code %d -> %s", raw, key_name(k));
    }
}

/* All the 23 cover styles on one screen, to compare with the Figma component (--copertine). */
static void cover_sheet(App *a)
{
    canvas_clear(&a->cv, G_CARTA);
    for (int i = 0; i < 23; i++) {
        float x = 8 + (i % 5) * 118, y = 8 + (i / 5) * 160;
        cover_draw(&a->cv, x, y, 106, 153.7f, "Il barone rampante", "Italo Calvino", i, 1);
    }
    full(a, REFRESH_FULL);
}

/* --------------------------------------------------------------- main */

static void on_signal(int sig)
{
    if (sig == SIGUSR1)
        g_power = 1;
    else if (sig == SIGUSR2)
        g_power = 2;
    else
        g_quit = 1;
}

static void usage(void)
{
    fprintf(stderr,
            "carbonella %s\n"
            "  --base DIR     cartella di Carbonella (default /mnt/us/carbonella)\n"
            "  --libri DIR    cartella dei libri (default dal file carbonella.conf)\n"
            "  --tasti        prova dei tasti\n"
            "  --ultimo       riapre subito l'ultimo libro letto\n"
            "  --sim DIR      simulatore: salva ogni schermata come PGM in DIR\n"
            "  --keys LIST    simulatore: tasti da premere (U D L R S N P H M B, W = attesa,\n"
            "                 Z = standby, A = risveglio)\n"
            "  --copertine    mostra i 23 stili di copertina\n"
            "Segnali: SIGUSR1 = il Kindle va in standby, SIGUSR2 = si risveglia.\n",
            CARBONELLA_VERSION);
}

int main(int argc, char **argv)
{
    App *a = xcalloc(1, sizeof *a);
    const char *sim = NULL, *keys = NULL, *books = NULL;
    int tasti = 0, ultimo = 0, sheet = 0;
    snprintf(a->base, sizeof a->base, "/mnt/us/carbonella");
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--base") && i + 1 < argc)
            snprintf(a->base, sizeof a->base, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--libri") && i + 1 < argc)
            books = argv[++i];
        else if (!strcmp(argv[i], "--sim") && i + 1 < argc)
            sim = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc)
            keys = argv[++i];
        else if (!strcmp(argv[i], "--tasti"))
            tasti = 1;
        else if (!strcmp(argv[i], "--copertine"))
            sheet = 1;
        else if (!strcmp(argv[i], "--versione")) {
            printf("carbonella %s\n", CARBONELLA_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--ultimo"))
            ultimo = 1;
        else {
            usage();
            return 2;
        }
    }
    if (sim && !keys)
        keys = "";
    g_sim = sim != NULL;

    struct sigaction sa = { 0 };
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    log_msg("Carbonella %s", CARBONELLA_VERSION);
    char path[1024];
    snprintf(path, sizeof path, "%s/carbonella.conf", a->base);
    settings_load(&a->cfg, path);
    snprintf(a->data, sizeof a->data, "%s/dati", a->base);
    mkdir_p(a->data);
    snprintf(a->prefs_path, sizeof a->prefs_path, "%s/preferenze.conf", a->data);
    settings_load(&a->cfg, a->prefs_path);
    snprintf(a->books, sizeof a->books, "%s", books ? books : settings_get(&a->cfg, "libri", "/mnt/us/documents"));
    snprintf(a->name, sizeof a->name, "%s", settings_get(&a->cfg, "nome", "Carbonella"));
    a->flash_every = settings_int(&a->cfg, "lampeggio_ogni", 6);
    if (a->flash_every < 1)
        a->flash_every = 1;
    int idle_min = settings_int(&a->cfg, "inattivita_minuti", 20);
    a->idle_ms = idle_min > 0 ? idle_min * 60000 : -1;
    a->goal_min = settings_int(&a->cfg, "obiettivo_minuti", 30);
    a->sort = settings_int(&a->cfg, "ordina", 0);
    a->books_year = settings_int(&a->cfg, "libri_anno", 12);
    if (a->sort < 0 || a->sort > 2)
        a->sort = 0;

    if (display_open(sim) != 0)
        return 1;
    display_set_flash_mode(settings_get(&a->cfg, "lampeggio", "area"));
    if (input_open(sim ? keys : NULL) != 0) {
        display_close();
        return 1;
    }
    snprintf(path, sizeof path, "%s/fonts", a->base);
    if (font_load(path) != 0) {
        log_msg("fonts missing in %s", path);
        input_close();
        display_close();
        return 1;
    }
    a->W = display_width();
    a->H = display_height();
    canvas_init(&a->cv, a->W, a->H);
    layout_from_settings(a);

    if (sheet) {
        cover_sheet(a);
    } else if (tasti) {
        key_test(a);
    } else {
        Next next = GO_LIBRARY;
        library_scan(&a->lib, a->books, a->data);
        if ((ultimo || settings_int(&a->cfg, "apri_ultimo", 0)) && a->lib.n > 0 && a->lib.e[0].percent >= 0 &&
            a->lib.e[0].percent < 100) {
            g_selected = &a->lib.e[0];
            next = GO_READER;
        }
        char focus_path[1024] = "";
        while (next != GO_EXIT && !g_quit) {
            if (next == GO_LIBRARY) {
                LibEntry *focus = focus_path[0] ? library_find(&a->lib, focus_path) : NULL;
                int r = grid_view(a, NULL, 0, focus);
                if (r == GO_LIBRARY) { /* "Aggiorna" from the menu */
                    library_scan(&a->lib, a->books, a->data);
                    focus_path[0] = 0;
                }
                next = r < 0 ? GO_LIBRARY : (Next)r;
            } else if (next == GO_READER && g_selected) {
                a->entry = g_selected;
                snprintf(focus_path, sizeof focus_path, "%s", a->entry->path);
                next = run_reader(a);
                a->entry = NULL;
                if (next == GO_LIBRARY)
                    library_scan(&a->lib, a->books, a->data); /* order by last read */
            } else
                next = GO_LIBRARY;
        }
        canvas_clear(&a->cv, G_CARTA);
        message(a, "Torno al Kindle…", "L'interfaccia Amazon riparte tra poco.", REFRESH_FULL);
    }
    input_close();
    display_close();
    library_free(&a->lib);
    log_msg("bye");
    return 0;
}
