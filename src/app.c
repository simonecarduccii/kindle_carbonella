/*
 * Carbonella - screens and main loop.
 *
 *   Libreria  list of books, most recently read first
 *   Lettura   the page, with a thin status line (chapter, page, percentage)
 *   Menu      text size, table of contents, back to the library, back to the Kindle
 *
 * Keys (Kindle 4):
 *   page keys / 5-way left-right   turn pages, move between list pages
 *   5-way up-down                  move in lists (also turns pages while reading)
 *   centre                         open / choose
 *   Menu                           menu
 *   Back                           go back one level
 *   Home                           library; from the library: back to the Kindle
 */
#include "carbonella.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

volatile int g_quit;

static const int SIZES[] = { 14, 16, 18, 20, 22, 24, 28 };
#define NSIZES ((int)(sizeof SIZES / sizeof SIZES[0]))

typedef struct {
    Settings cfg;
    char base[512], data[600], books[512], prefs_path[700];
    Canvas cv;
    int W, H;
    Library lib;
    LayoutParams lp;
    int flash_every, idle_ms;
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
} App;

typedef enum { GO_LIBRARY, GO_READER, GO_EXIT } Next;

/* ------------------------------------------------------------ helpers */

static void layout_from_settings(App *a)
{
    int size = settings_int(&a->cfg, "dimensione_testo", 18);
    a->lp = (LayoutParams){
        .width = a->W,
        .height = a->H,
        .margin_x = settings_int(&a->cfg, "margine", 40),
        .margin_top = 44,
        .margin_bottom = 58,
        .size = size,
        .line_pct = settings_int(&a->cfg, "interlinea", 145),
        .justify = settings_int(&a->cfg, "giustifica", 1),
        .indent = size * 13 / 10,
    };
}

static void save_prefs(App *a)
{
    Settings p = { 0 };
    settings_load(&p, a->prefs_path); /* keep whatever else the user wrote there */
    settings_set_int(&p, "dimensione_testo", a->lp.size);
    settings_save(&p, a->prefs_path, "# Preferenze personali: valgono più di carbonella.conf e restano dopo gli aggiornamenti.");
    for (int i = 0; i < p.n; i++) {
        free(p.key[i]);
        free(p.val[i]);
    }
}

/* A centred message box; used for "opening...", "back to Kindle..." */
static void message(App *a, const char *title, const char *line2, RefreshKind kind)
{
    int w = 460, h = line2 ? 130 : 96;
    int x = (a->W - w) / 2, y = (a->H - h) / 2;
    canvas_fill(&a->cv, x, y, w, h, INK_PAPER);
    canvas_frame(&a->cv, x, y, w, h, 3, INK_BLACK);
    int tw = font_text_width(FACE_BOLD, 22, title);
    font_draw_text(&a->cv, FACE_BOLD, 22, x + (w - (tw < w - 40 ? tw : w - 40)) / 2, y + 54, title, INK_BLACK,
                   w - 40);
    if (line2) {
        int lw = font_text_width(FACE_REGULAR, 17, line2);
        font_draw_text(&a->cv, FACE_REGULAR, 17, x + (w - (lw < w - 40 ? lw : w - 40)) / 2, y + 94, line2,
                       INK_DARK, w - 40);
    }
    display_update(&a->cv, (Rect){ x, y, w, h }, kind);
}

/* ----------------------------------------------------------- list UI */

typedef struct {
    const char *title, *sub, *right;
} ListItem;

typedef struct {
    const char *header, *header_right, *hint;
    Rect area;
    int row_h;
    int title_size;
} ListLook;

static int rows_per_page(const ListLook *lk)
{
    int top = lk->header ? 96 : 16;
    return (lk->area.h - top - 50) / lk->row_h;
}

static Rect row_rect(const ListLook *lk, int slot)
{
    int top = lk->header ? 96 : 16;
    return (Rect){ lk->area.x, lk->area.y + top + slot * lk->row_h, lk->area.w, lk->row_h };
}

static void draw_row(App *a, const ListLook *lk, const ListItem *it, int slot, int selected)
{
    Rect r = row_rect(lk, slot);
    int pad = 32;
    uint8_t fg = selected ? INK_PAPER : INK_BLACK;
    uint8_t fg2 = selected ? INK_PAPER : INK_DARK;
    canvas_fill(&a->cv, r.x, r.y, r.w, r.h, selected ? INK_BLACK : INK_PAPER);
    int right_w = it->right ? font_text_width(FACE_REGULAR, 17, it->right) + 16 : 0;
    int text_w = r.w - 2 * pad - right_w;
    if (it->sub && *it->sub) {
        font_draw_text(&a->cv, FACE_BOLD, lk->title_size, r.x + pad, r.y + r.h / 2 - 4, it->title, fg, text_w);
        font_draw_text(&a->cv, FACE_REGULAR, 17, r.x + pad, r.y + r.h / 2 + 22, it->sub, fg2, text_w);
    } else {
        font_draw_text(&a->cv, FACE_REGULAR, lk->title_size, r.x + pad, r.y + r.h / 2 + lk->title_size / 3,
                       it->title, fg, text_w);
    }
    if (it->right)
        font_draw_text(&a->cv, FACE_REGULAR, 17, r.x + r.w - pad - right_w + 16, r.y + r.h / 2 + 6, it->right,
                       fg2, 0);
    if (!selected)
        canvas_hline(&a->cv, r.x + pad, r.y + r.h - 2, r.w - 2 * pad, 2, INK_MID);
}

static void draw_list_page(App *a, const ListLook *lk, const ListItem *items, int n, int sel)
{
    Rect ar = lk->area;
    int per = rows_per_page(lk);
    int first = (sel / per) * per;
    canvas_fill(&a->cv, ar.x, ar.y, ar.w, ar.h, INK_PAPER);
    if (lk->area.y > 0)
        canvas_hline(&a->cv, ar.x, ar.y, ar.w, 3, INK_BLACK);
    if (lk->header) {
        font_draw_text(&a->cv, FACE_BOLD, 30, ar.x + 32, ar.y + 62, lk->header, INK_BLACK, ar.w - 200);
        if (lk->header_right) {
            int w = font_text_width(FACE_REGULAR, 17, lk->header_right);
            font_draw_text(&a->cv, FACE_REGULAR, 17, ar.x + ar.w - 32 - w, ar.y + 60, lk->header_right, INK_DARK, 0);
        }
        canvas_hline(&a->cv, ar.x + 32, ar.y + 86, ar.w - 64, 3, INK_BLACK);
    }
    for (int i = 0; i < per && first + i < n; i++)
        draw_row(a, lk, &items[first + i], i, first + i == sel);
    /* footer */
    int fy = ar.y + ar.h - 20;
    if (lk->hint)
        font_draw_text(&a->cv, FACE_REGULAR, 15, ar.x + 32, fy, lk->hint, INK_DARK, ar.w - 160);
    if (n > per) {
        char pg[32];
        snprintf(pg, sizeof pg, "%d / %d", first / per + 1, (n + per - 1) / per);
        int w = font_text_width(FACE_REGULAR, 15, pg);
        font_draw_text(&a->cv, FACE_REGULAR, 15, ar.x + ar.w - 32 - w, fy, pg, INK_DARK, 0);
    }
}

/* Runs a list. Returns the chosen index, -1 for Back, -2 for another key (in *key). */
static int list_run(App *a, const ListLook *lk, const ListItem *items, int n, int sel, Key *key, int full)
{
    int per = rows_per_page(lk);
    if (sel >= n)
        sel = n - 1;
    if (sel < 0)
        sel = 0;
    draw_list_page(a, lk, items, n, sel);
    display_update(&a->cv, lk->area, full ? REFRESH_FULL : REFRESH_PARTIAL);
    for (;;) {
        int raw;
        Key k = input_wait(a->idle_ms, &raw);
        int old = sel;
        switch (k) {
        case K_UP: sel = sel > 0 ? sel - 1 : n - 1; break;
        case K_DOWN: sel = sel < n - 1 ? sel + 1 : 0; break;
        case K_LEFT:
        case K_PREV: sel = sel - per >= 0 ? (sel / per - 1) * per : sel; break;
        case K_RIGHT:
        case K_NEXT: sel = (sel / per + 1) * per < n ? (sel / per + 1) * per : sel; break;
        case K_SELECT:
            if (n > 0)
                return sel;
            break;
        case K_BACK: return -1;
        case K_NONE: break;
        default:
            if (key)
                *key = k;
            return -2;
        }
        if (n == 0 || sel == old)
            continue;
        if (sel / per != old / per) {
            draw_list_page(a, lk, items, n, sel);
            display_update(&a->cv, lk->area, REFRESH_PARTIAL);
        } else {
            draw_row(a, lk, &items[old], old % per, 0);
            draw_row(a, lk, &items[sel], sel % per, 1);
            Rect r1 = row_rect(lk, old % per), r2 = row_rect(lk, sel % per);
            int y0 = r1.y < r2.y ? r1.y : r2.y;
            int y1 = (r1.y > r2.y ? r1.y : r2.y) + lk->row_h;
            display_update(&a->cv, (Rect){ lk->area.x, y0, lk->area.w, y1 - y0 }, REFRESH_PARTIAL);
        }
    }
}

static int confirm_exit(App *a)
{
    ListLook lk = { .header = "Tornare al Kindle?", .area = { 0, 0, a->W, a->H }, .row_h = 76, .title_size = 22,
                    .hint = "L'interfaccia Amazon riparte in circa 30 secondi." };
    ListItem items[] = { { "No, resta in Carbonella", NULL, NULL }, { "Sì, torna al Kindle", NULL, NULL } };
    Key k;
    int r = list_run(a, &lk, items, 2, 0, &k, 0);
    return r == 1 || (r == -2 && k == K_TIMEOUT);
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

static void save_position(App *a)
{
    if (!a->entry || !a->ch || a->pg.n == 0)
        return;
    a->entry->spine = a->spine;
    a->entry->pos = a->pg.starts[a->page].pos;
    a->entry->percent = percent_of(a);
    a->entry->last_read = time(NULL);
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
    if (which_page == 0)
        a->page = 0;
    else if (which_page == -1)
        a->page = a->pg.n - 1;
    else
        a->page = pagination_find(&a->pg, pos);
    return 0;
}

static const char *chapter_title(App *a)
{
    /* several entries can point into the same file (anchors): take the first one of this file,
     * otherwise the last one before it */
    const char *best = NULL;
    for (int i = 0; i < a->book->ntoc; i++) {
        if (a->book->toc[i].spine == a->spine)
            return a->book->toc[i].title;
        if (a->book->toc[i].spine < a->spine)
            best = a->book->toc[i].title;
    }
    return best ? best : a->book->title;
}

static void render_page(App *a, RefreshKind kind)
{
    canvas_clear(&a->cv, INK_PAPER);
    if (a->ch && a->pg.n > 0)
        layout_page(&a->lp, a->ch, a->pg.starts[a->page], &a->cv);
    /* status line */
    int y = a->H - 22;
    char right[64];
    snprintf(right, sizeof right, "%d / %d  ·  %d%%", a->page + 1, a->pg.n, percent_of(a));
    int rw = font_text_width(FACE_REGULAR, 14, right);
    font_draw_text(&a->cv, FACE_REGULAR, 14, a->W - a->lp.margin_x - rw, y, right, INK_DARK, 0);
    font_draw_text(&a->cv, FACE_REGULAR, 14, a->lp.margin_x, y, chapter_title(a), INK_DARK,
                   a->W - 2 * a->lp.margin_x - rw - 24);
    if (a->ch && a->ch->nblocks == 0)
        font_draw_text(&a->cv, FACE_ITALIC, 18, a->lp.margin_x, a->H / 2, "(pagina senza testo)", INK_DARK, 0);
    display_update(&a->cv, (Rect){ 0, 0, a->W, a->H }, kind);
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
    if (++a->turns >= a->flash_every) {
        kind = REFRESH_FULL;
    }
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
            items[i].title = a->book->toc[i].title;
            if (a->book->toc[i].spine < a->spine || (a->book->toc[i].spine == a->spine &&
                                                       (i == 0 || a->book->toc[i - 1].spine != a->spine)))
                sel = i;
        } else {
            labels[i] = xmalloc(32);
            snprintf(labels[i], 32, "Sezione %d", i + 1);
            items[i].title = labels[i];
            if (i == a->spine)
                sel = i;
        }
    }
    ListLook lk = { .header = "Indice", .area = { 0, 0, a->W, a->H }, .row_h = 64, .title_size = 19,
                    .hint = "Centro: vai  ·  Indietro: torna alla pagina" };
    int r = list_run(a, &lk, items, n, sel, NULL, 1);
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

static void change_size(App *a, int delta)
{
    int i = size_index(a->lp.size) + delta;
    if (i < 0 || i >= NSIZES)
        return;
    uint32_t pos = a->pg.n ? a->pg.starts[a->page].pos : 0;
    a->lp.size = SIZES[i];
    a->lp.indent = a->lp.size * 13 / 10;
    paginate(&a->lp, a->ch, &a->pg);
    a->page = pagination_find(&a->pg, pos);
    save_prefs(a);
}

/* Returns where to go after the menu, or -1 to keep reading. */
static int reader_menu(App *a)
{
    int sel = 0;
    for (;;) {
        char size_label[48];
        snprintf(size_label, sizeof size_label, "Testo: %d px", a->lp.size);
        ListItem items[] = {
            { "Indice", NULL, NULL },
            { "Testo più grande", NULL, NULL },
            { "Testo più piccolo", NULL, NULL },
            { "Pulisci lo schermo", NULL, NULL },
            { "Torna alla libreria", NULL, NULL },
            { "Torna al Kindle", NULL, NULL },
        };
        int n = sizeof items / sizeof items[0];
        int h = 16 + n * 60 + 50;
        ListLook lk = { .area = { 0, a->H - h, a->W, h }, .row_h = 60, .title_size = 20, .hint = size_label };
        Key k;
        int r = list_run(a, &lk, items, n, sel, &k, 0);
        switch (r) {
        case 0: show_toc(a); return -1;
        case 1:
        case 2:
            change_size(a, r == 1 ? 1 : -1);
            render_page(a, REFRESH_PARTIAL);
            sel = r;
            continue; /* menu stays open to change again */
        case 3: return -1;
        case 4: return GO_LIBRARY;
        case 5: return GO_EXIT;
        case -2:
            if (k == K_HOME)
                return GO_LIBRARY;
            if (k == K_TIMEOUT || k == K_QUIT)
                return GO_EXIT;
            return -1;
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
        Key k = input_wait(a->idle_ms, &raw);
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
    close_book(a);
    return next;
}

/* ------------------------------------------------------------ library */

static LibEntry *g_selected;

static Next run_library(App *a, int *sel)
{
    int n = a->lib.n;
    ListItem *items = xcalloc(n ? n : 1, sizeof(ListItem));
    char (*right)[16] = xcalloc(n ? n : 1, 16);
    for (int i = 0; i < n; i++) {
        LibEntry *e = &a->lib.e[i];
        items[i].title = e->title;
        items[i].sub = e->author;
        if (e->percent >= 100)
            snprintf(right[i], 16, "letto");
        else if (e->percent >= 0)
            snprintf(right[i], 16, "%d%%", e->percent);
        else
            snprintf(right[i], 16, "nuovo");
        items[i].right = right[i];
    }
    char count[32];
    snprintf(count, sizeof count, n == 1 ? "%d libro" : "%d libri", n);
    ListLook lk = { .header = "Carbonella", .header_right = count, .area = { 0, 0, a->W, a->H }, .row_h = 88,
                    .title_size = 21, .hint = "Centro: leggi  ·  Menu: opzioni  ·  Home: esci" };
    Next next = GO_LIBRARY;
    for (;;) {
        if (n == 0) {
            canvas_clear(&a->cv, INK_PAPER);
            font_draw_text(&a->cv, FACE_BOLD, 30, 32, 62, "Carbonella", INK_BLACK, 0);
            canvas_hline(&a->cv, 32, 86, a->W - 64, 3, INK_BLACK);
            font_draw_text(&a->cv, FACE_BOLD, 22, 32, 200, "Nessun libro, per ora.", INK_BLACK, 0);
            font_draw_text(&a->cv, FACE_REGULAR, 18, 32, 250, "Collega il Kindle al computer e copia", INK_BLACK, 0);
            font_draw_text(&a->cv, FACE_REGULAR, 18, 32, 278, "file .epub o .txt nella cartella", INK_BLACK, 0);
            font_draw_text(&a->cv, FACE_BOLD, 18, 32, 306, "documents.", INK_BLACK, 0);
            font_draw_text(&a->cv, FACE_REGULAR, 18, 32, 360, "Premi Home per tornare al Kindle.", INK_DARK, 0);
            display_update(&a->cv, (Rect){ 0, 0, a->W, a->H }, REFRESH_FULL);
            Key k = input_wait(a->idle_ms, NULL);
            if (k == K_HOME || k == K_TIMEOUT || k == K_QUIT) {
                next = GO_EXIT;
                break;
            }
            continue;
        }
        Key k = K_NONE;
        int r = list_run(a, &lk, items, n, *sel, &k, 1);
        if (r >= 0) {
            *sel = r;
            g_selected = &a->lib.e[r];
            next = GO_READER;
            break;
        }
        if (r == -2 && (k == K_TIMEOUT || k == K_QUIT)) {
            next = GO_EXIT;
            break;
        }
        if (r == -2 && k == K_HOME) {
            if (confirm_exit(a)) {
                next = GO_EXIT;
                break;
            }
        } else if (r == -2 && k == K_MENU) {
            char ver[64];
            snprintf(ver, sizeof ver, "Carbonella %s", CARBONELLA_VERSION);
            ListItem m[] = { { "Aggiorna l'elenco dei libri", NULL, NULL }, { "Torna al Kindle", NULL, NULL } };
            int h = 16 + 2 * 60 + 50;
            ListLook ml = { .area = { 0, a->H - h, a->W, h }, .row_h = 60, .title_size = 20, .hint = ver };
            Key mk;
            int mr = list_run(a, &ml, m, 2, 0, &mk, 0);
            if (mr == 0) {
                next = GO_LIBRARY; /* rescan */
                *sel = 0;
                g_selected = NULL;
                break;
            }
            if (mr == 1) {
                next = GO_EXIT;
                break;
            }
        }
    }
    free(items);
    free(right);
    return next;
}

/* --------------------------------------------------------- key test */

static void key_test(App *a)
{
    int homes = 0;
    char lines[12][64];
    int nl = 0;
    for (;;) {
        canvas_clear(&a->cv, INK_PAPER);
        font_draw_text(&a->cv, FACE_BOLD, 30, 32, 62, "Prova dei tasti", INK_BLACK, 0);
        canvas_hline(&a->cv, 32, 86, a->W - 64, 3, INK_BLACK);
        font_draw_text(&a->cv, FACE_REGULAR, 18, 32, 130, "Premi i tasti uno alla volta.", INK_BLACK, 0);
        font_draw_text(&a->cv, FACE_REGULAR, 18, 32, 158, "Home tre volte di fila per uscire.", INK_BLACK, 0);
        for (int i = 0; i < nl; i++)
            font_draw_text(&a->cv, FACE_REGULAR, 22, 32, 230 + i * 40, lines[i], INK_BLACK, 0);
        display_update(&a->cv, (Rect){ 0, 0, a->W, a->H }, REFRESH_PARTIAL);
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

/* --------------------------------------------------------------- main */

static void on_signal(int sig)
{
    (void)sig;
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
            "  --keys LIST    simulatore: tasti da premere (U D L R S N P H M B, W = attesa)\n",
            CARBONELLA_VERSION);
}

int main(int argc, char **argv)
{
    App *a = xcalloc(1, sizeof *a);
    const char *sim = NULL, *keys = NULL, *books = NULL;
    int tasti = 0, ultimo = 0;
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
        else if (!strcmp(argv[i], "--ultimo"))
            ultimo = 1;
        else {
            usage();
            return 2;
        }
    }
    if (sim && !keys)
        keys = "";

    struct sigaction sa = { 0 };
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    log_msg("Carbonella %s", CARBONELLA_VERSION);
    char path[1024];
    snprintf(path, sizeof path, "%s/carbonella.conf", a->base);
    settings_load(&a->cfg, path);
    snprintf(a->data, sizeof a->data, "%s/dati", a->base);
    mkdir_p(a->data);
    snprintf(a->prefs_path, sizeof a->prefs_path, "%s/preferenze.conf", a->data);
    settings_load(&a->cfg, a->prefs_path);
    snprintf(a->books, sizeof a->books, "%s", books ? books : settings_get(&a->cfg, "libri", "/mnt/us/documents"));
    a->flash_every = settings_int(&a->cfg, "lampeggio_ogni", 6);
    if (a->flash_every < 1)
        a->flash_every = 1;
    int idle_min = settings_int(&a->cfg, "inattivita_minuti", 20);
    a->idle_ms = idle_min > 0 ? idle_min * 60000 : -1;

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

    if (tasti) {
        key_test(a);
    } else {
        int sel = 0;
        Next next = GO_LIBRARY;
        int rescan = 1;
        if (ultimo || settings_int(&a->cfg, "apri_ultimo", 0)) {
            library_scan(&a->lib, a->books, a->data);
            rescan = 0;
            if (a->lib.n > 0 && a->lib.e[0].percent >= 0) {
                g_selected = &a->lib.e[0];
                next = GO_READER;
            }
        }
        while (next != GO_EXIT && !g_quit) {
            if (next == GO_LIBRARY) {
                if (rescan) {
                    library_scan(&a->lib, a->books, a->data);
                    if (g_selected == NULL)
                        sel = 0;
                }
                rescan = 0;
                next = run_library(a, &sel);
                if (next == GO_LIBRARY) /* "Aggiorna" from the menu */
                    rescan = 1;
            } else if (next == GO_READER && g_selected) {
                a->entry = g_selected;
                next = run_reader(a);
                if (next == GO_LIBRARY) {
                    rescan = 1; /* order by last read */
                    g_selected = NULL;
                    sel = 0;
                }
            } else
                next = GO_LIBRARY;
        }
        canvas_clear(&a->cv, INK_PAPER);
        message(a, "Torno al Kindle…", "L'interfaccia Amazon riparte tra poco.", REFRESH_FULL);
    }
    input_close();
    display_close();
    library_free(&a->lib);
    log_msg("bye");
    return 0;
}
