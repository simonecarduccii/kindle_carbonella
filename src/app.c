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
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

volatile int g_quit;
volatile int g_power;

static const int SIZES[] = { 14, 16, 18, 20, 22, 24, 28 };
#define NSIZES ((int)(sizeof SIZES / sizeof SIZES[0]))
static const int GOALS[] = { 10, 15, 20, 30, 45, 60, 90 };
static const int STANDBYS[] = { 1, 2, 3, 5, 10 }; /* minutes; powerd's own timer is about 10 */
#define NSTANDBYS ((int)(sizeof STANDBYS / sizeof STANDBYS[0]))
#define NGOALS ((int)(sizeof GOALS / sizeof GOALS[0]))
static const char *SORTS[] = { "Più recente", "Titolo", "Autore" };
static const int LINES[] = { 130, 140, 150, 160, 180 };
#define NLINES ((int)(sizeof LINES / sizeof LINES[0]))
static const int MARGINS[] = { 32, 48, 64 };
static const char *MARGIN_NAMES[] = { "Stretti", "Medi", "Larghi" };
/* "Libri da leggere nell'anno": one book per press (Simone, 18 nov), from 1 to 99 */
static int year_step(int v, int dir) { v += dir; return v < 1 ? 1 : v > 99 ? 99 : v; }

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
    int standby_min; /* "Standby automatico dopo": minutes without keys, 0 = never */
    double goal_note_until; /* now_ms() until which "N min oggi" stays on the page (0 = hidden) */
    long goal_note_day;     /* day on which the goal was last announced */
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
    long long last_turn; /* last page turn of the session (reading time pauses after READ_IDLE_S) */
    int session_pages;
    double cal_bytes, cal_pages; /* calibration of bytes per page, from the chapters paginated */
    int cal_last_spine;
    /* highlights (Simone, 18 nov): the saved ones of the open book, and the selection being made */
    struct Highlight *hls;
    int nhls;
    TextMark sel;             /* kind 0 = no selection */
    int caret_x, caret_top, caret_h; /* blinking cursor, caret_h 0 = none */
    const char *foot;         /* text of the footer instead of "12 di 684" */
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
    settings_set_int(&p, "standby_minuti", a->standby_min);
    settings_set_int(&p, "percentuale_batteria", g_battery_pct);
    settings_save(&p, a->prefs_path, "# Preferenze personali: valgono più di carbonella.conf e restano dopo gli aggiornamenti.");
    free_settings(&p);
}

static void full(App *a, RefreshKind kind)
{
    display_update(&a->cv, (Rect){ 0, 0, a->W, a->H }, kind);
}

/* Minutes read today (sessions in letture.tsv, plus the one in progress). */
/* Simone (17 nov): if no page is turned for READ_IDLE_S, the reading time stops counting. The session
 * counts up to the last page turn plus at most READ_IDLE_S; the next turn starts a new session. */
#define READ_IDLE_S (5 * 60)
static long long session_end(const App *a, long long now)
{
    long long cap = a->last_turn + READ_IDLE_S;
    return now < cap ? now : cap;
}

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
    if (a->book && a->session_start) {
        long long d = session_end(a, now) - (a->session_start > midnight ? a->session_start : midnight);
        secs += d > 0 ? d : 0;
    }
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
        cover_draw(&a->cv, 0, 0, a->W, a->H, e->title, e->author, e->publisher, 0);
    else
        cover_draw(&a->cv, 0, 0, a->W, a->H, "Carbonella", a->name, "", 0);
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
/* Tell powerd that someone is reading, so the automatic standby waits (at most once a minute: on the
 * K4 lipc costs a few tens of ms, so it runs in the background). */
static void touch_screensaver(void)
{
    static time_t last;
    time_t now = time(NULL);
    if (g_sim || now - last < 60)
        return;
    last = now;
    if (system("lipc-set-prop com.lab126.powerd touchScreenSaverTimeout 1 >/dev/null 2>&1 &") != 0)
        log_msg("touchScreenSaverTimeout failed");
}

/* Simone (17 nov): the automatic standby must not start while he is reading. With a book open the
 * "Standby automatico dopo" timer is replaced by READING_STANDBY_MS without keys, a safety net for a
 * reader who fell asleep. Meanwhile powerd's own timer (about 10 min) is touched every
 * READING_TOUCH_MS, so it cannot fire in the middle of a long page either. */
#define READING_STANDBY_MS (30 * 60000)
#define READING_TOUCH_MS (4 * 60000)

/* "Standby automatico dopo": no key for standby_min minutes. powerd on the K4 has no lipc property for
 * the power button, so we use its test tool to simulate a press ("powerd_test -p"); powerd then sends
 * goingToScreenSaver and the cover appears as usual. */
static int auto_standby(App *a)
{
    log_msg("no key for %d min: standby", a->book ? READING_STANDBY_MS / 60000 : a->standby_min);
    if (system("powerd_test -p >/dev/null 2>&1") != 0) {
        log_msg("powerd_test -p failed");
        return -1;
    }
    return 0;
}

/* v1.0.3 "light standby": in powerd's screen saver the K4 keypad is switched off (no interrupts at
 * all, measured 2026-11-17), so the Home key cannot wake it. So the automatic standby first only shows
 * the cover while Carbonella stays awake: any key (Home too) brings the page back. After
 * LIGHT_STANDBY_MS without keys the real standby of powerd starts (deep sleep, only the power button
 * wakes). Returns the key that ended it: K_TIMEOUT, K_SLEEP (power button) or K_QUIT leave the page
 * in the canvas without drawing it. */
#define LIGHT_STANDBY_MS (3 * 60000)
static Key light_standby(App *a)
{
    uint8_t *saved = xmalloc((size_t)a->W * a->H);
    memcpy(saved, a->cv.px, (size_t)a->W * a->H);
    LibEntry *e = a->entry && a->book ? a->entry : (a->lib.n > 0 ? &a->lib.e[0] : NULL);
    canvas_clear(&a->cv, G_CARTA);
    if (e)
        cover_draw(&a->cv, 0, 0, a->W, a->H, e->title, e->author, e->publisher, 0);
    else
        cover_draw(&a->cv, 0, 0, a->W, a->H, "Carbonella", a->name, "", 0);
    full(a, REFRESH_FULL);
    log_msg("light standby");
    Key k = input_wait(LIGHT_STANDBY_MS, NULL);
    memcpy(a->cv.px, saved, (size_t)a->W * a->H);
    free(saved);
    if (k == K_TIMEOUT || k == K_SLEEP || k == K_QUIT) /* the page is back in the canvas, not on screen */
        return k;
    log_msg("light standby: key, wake up");
    full(a, REFRESH_FULL);
    return k;
}

static void draw_page(App *a);
static Key wait_key(App *a, int *raw)
{
    int t = a->standby_seen ? -1 : a->idle_ms, sb = 0;
    int reading = a->book != NULL;
    int limit = reading ? READING_STANDBY_MS : a->standby_min * 60000;
    if (!g_sim && limit > 0 && (t < 0 || limit < t)) {
        t = limit;
        sb = 1;
    }
    Key k = K_TIMEOUT;
    int got = 0;
    if (reading && a->goal_note_until > 0) { /* "N min oggi" goes away after GOAL_NOTE_MS */
        int left = (int)(a->goal_note_until - now_ms());
        if (left > 0 && (k = input_wait(left, raw)) != K_TIMEOUT)
            got = 1;
        else {
            a->goal_note_until = 0;
            draw_page(a);
            display_update(&a->cv, (Rect){ 0, 0, a->W, 48 }, REFRESH_PARTIAL);
        }
    }
    if (got)
        ;
    else if (reading && sb) { /* wait in slices, keeping powerd awake, until a key or the safety net */
        int left = t;
        for (;;) {
            int slice = left < READING_TOUCH_MS ? left : READING_TOUCH_MS;
            k = input_wait(slice, raw);
            left -= slice;
            if (k != K_TIMEOUT || left <= 0)
                break;
            touch_screensaver();
        }
    } else
        k = input_wait(t, raw);
    if (k == K_TIMEOUT && sb) {
        Key w = light_standby(a);
        if (w == K_TIMEOUT) { /* powerd's goingToScreenSaver arrives next: standby() shows the cover */
            log_msg("light standby over: real standby");
            if (auto_standby(a) != 0)
                full(a, REFRESH_FULL); /* no real standby: the page comes back */
        } else if (w == K_SLEEP) /* the power button during the light standby */
            standby(a);
        else if (w == K_QUIT)
            return K_QUIT;
        return K_NONE; /* the key that woke it does nothing else */
    }
    if (k != K_TIMEOUT && k != K_SLEEP && k != K_WAKE)
        touch_screensaver(); /* someone is reading: powerd's own timer starts again */
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
 * Rows in 'steppers' (bit i = row i) show their value as "‹  value  ›" and change with left/right.
 * Returns the chosen row (in *sel too); -1 closed; -2 (+ *key) for Left/Right on a stepper row;
 * PANEL_END (+ *key) for Home, timeout, quit. */
static int panel_run(App *a, Row *rows, int n, int *selp, const char *footer_l, const char *footer_c,
                     const char *footer_r, double frac, unsigned steppers, Key *key)
{
    int h = 1 + 16 + n * 56 + 16 + 48;
    int y0 = a->H - h;
    int sel = *selp;
    for (;;) {
        *selp = sel;
        canvas_fill(&a->cv, 0, y0, a->W, h, G_CARTA);
        canvas_fill(&a->cv, 0, y0, a->W, 1, G_CENERE);
        for (int i = 0; i < n; i++) {
            char val[64];
            const char *v = rows[i].value;
            int step = (steppers >> i) & 1;
            if (step) {
                snprintf(val, sizeof val, "\xE2\x80\xB9  %s  \xE2\x80\xBA", rows[i].value);
                v = val;
            }
            ui_menu_row(&a->cv, 16, y0 + 17 + i * 56, a->W - 32, rows[i].label, v, step ? ICON_NONE : rows[i].icon,
                        i == sel);
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
            if ((k == K_LEFT || k == K_RIGHT) && ((steppers >> sel) & 1)) {
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

/* Small "Pannello menu" in the middle of the screen, over the screen faded to 30% (frame
 * "00 Home Menù Raccolta"): 424 px wide, rows of 56 px. Returns the chosen row, -1 for Back. */
#define POPUP_MAX 11
static int popup_run(App *a, Row *rows, int n, int sel)
{
    if (n > POPUP_MAX)
        n = POPUP_MAX;
    for (size_t i = 0; i < (size_t)a->W * a->H; i++) /* the screen behind at 30% */
        a->cv.px[i] = (uint8_t)(a->cv.px[i] * 30 / 100);
    int w = 424, h = 16 + n * 56 + 16;
    int x = (a->W - w) / 2, y = (a->H - h) / 2;
    canvas_fill(&a->cv, x, y, w, h, G_CARTA);
    canvas_frame(&a->cv, x - 1, y - 1, w + 2, h + 2, 1, G_GRAFITE); /* 1 px #222 outside, Simone v0.6.2 */
    int first = 1;
    for (;;) {
        for (int i = 0; i < n; i++)
            ui_menu_row(&a->cv, x + 16, y + 16 + i * 56, w - 32, rows[i].label, rows[i].value, rows[i].icon, i == sel);
        if (first)
            full(a, REFRESH_PARTIAL);
        else
            display_update(&a->cv, (Rect){ x, y, w, h }, REFRESH_PARTIAL);
        first = 0;
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
            if (k == K_SELECT || k == K_RIGHT)
                return sel;
            if (k == K_BACK || k == K_MENU || k == K_HOME || k == K_LEFT || k == K_TIMEOUT || k == K_QUIT)
                return -1;
        }
    }
}

/* "Sei sicuro di voler archiviare?" (frame "00 Home Menù Archivio", v1.0.4): the small panel, 424 px
 * wide, the question centred in a row of 56 px, then "Si" and "no" side by side (189 x 56 each, the
 * focused one black). Left/right move, centre chooses, Back = no. Returns 1 for Si. */
static int confirm_popup(App *a, const char *question)
{
    for (size_t i = 0; i < (size_t)a->W * a->H; i++) /* the screen behind at 30% */
        a->cv.px[i] = (uint8_t)(a->cv.px[i] * 30 / 100);
    int w = 424, h = 167, x = (a->W - w) / 2, y = (a->H - h) / 2;
    canvas_fill(&a->cv, x, y, w, h, G_CARTA);
    canvas_frame(&a->cv, x - 1, y - 1, w + 2, h + 2, 1, G_GRAFITE);
    int tw = font_text_width(FACE_SANS_MEDIUM, 20, question);
    font_draw_text(&a->cv, FACE_SANS_MEDIUM, 20, x + (w - tw) / 2, ui_baseline(FACE_SANS_MEDIUM, 20, y + 31, 25),
                   question, G_NERO, w - 40);
    int sel = 0, first = 1;
    for (;;) {
        static const char *L[2] = { "Si", "no" };
        for (int i = 0; i < 2; i++) {
            int bx = x + 21 + i * 194, by = y + 90;
            canvas_fill(&a->cv, bx, by, 189, 56, i == sel ? G_NERO : G_CARTA);
            int lw = font_text_width(FACE_SANS_MEDIUM, 20, L[i]);
            font_draw_text(&a->cv, FACE_SANS_MEDIUM, 20, bx + (189 - lw) / 2,
                           ui_baseline(FACE_SANS_MEDIUM, 20, by + 15, 25), L[i], i == sel ? G_CARTA : G_NERO, 0);
        }
        if (first)
            full(a, REFRESH_PARTIAL);
        else
            display_update(&a->cv, (Rect){ x, y + 90, w, 56 }, REFRESH_PARTIAL);
        first = 0;
        Key k = wait_key(a, NULL);
        if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN)
            sel = !sel;
        else if (k == K_SELECT)
            return sel == 0;
        else if (k == K_BACK || k == K_MENU || k == K_HOME || k == K_TIMEOUT || k == K_QUIT)
            return 0;
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
    free(a->hls);
    a->hls = NULL;
    a->nhls = 0;
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

#define GOAL_NOTE_MS 5000
static long day_number(long long t);
static int page_has_bookmark(App *a);
static void draw_bookmark_corner(App *a);
static int page_marks(App *a, TextMark *out, int max);

static void draw_page(App *a)
{
    canvas_clear(&a->cv, G_CARTA);
    TextMark marks[129];
    int nm = page_marks(a, marks, 128);
    if (a->sel.kind)
        marks[nm++] = a->sel;
    layout_marks = marks;
    layout_nmarks = nm;
    if (a->ch && a->pg.n > 0)
        layout_page(&a->lp, a->ch, a->pg.starts[a->page], &a->cv);
    layout_marks = NULL;
    layout_nmarks = 0;
    if (a->caret_h) /* "Evidenzia 1": 4 px bar, #222, the height of the line */
        canvas_fill(&a->cv, a->caret_x, a->caret_top, 4, a->caret_h, G_GRAFITE);
    if (a->ch && a->ch->nblocks == 0)
        font_draw_text(&a->cv, FACE_ITALIC, 18, a->lp.margin_x, a->H / 2, "(pagina senza testo)", G_GRAFITE, 0);
    int p, t;
    book_pages(a, &p, &t);
    char pos[48];
    snprintf(pos, sizeof pos, "%d di %d", p, t);
    if (page_has_bookmark(a))
        draw_bookmark_corner(a);
    /* Simone (17 nov): "■ N min oggi" shows for 5 seconds, only when the daily goal is reached */
    if (a->goal_min > 0 && a->goal_note_day != day_number(time(NULL)) && minutes_today(a) >= a->goal_min) {
        a->goal_note_day = day_number(time(NULL));
        a->goal_note_until = now_ms() + GOAL_NOTE_MS;
    }
    if (a->goal_note_until > now_ms()) {
        char t[32];
        snprintf(t, sizeof t, "%d min oggi", minutes_today(a));
        int tw = font_text_width(FACE_MONO, 16, t);
        int x = (a->W - (8 + 6 + tw)) / 2;
        canvas_fill(&a->cv, x, 20, 8, 8, G_NERO); /* black square since v0.6 */
        font_draw_text(&a->cv, FACE_MONO, 16, x + 14, ui_baseline(FACE_MONO, 16, 15, 19.2f), t, G_CENERE, 0);
    } else
        a->goal_note_until = 0;
    ui_progress(&a->cv, a->lp.margin_x, a->W - 2 * a->lp.margin_x, a->H, NULL, a->foot ? a->foot : pos, NULL,
                percent_of(a) / 100.0,
                G_SMOG); /* track #999 since v0.6 */
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
    long long now = time(NULL);
    if (a->session_start && now - a->last_turn > READ_IDLE_S) { /* back after a pause: new session */
        reading_log(a->data, a->entry->path, a->session_start, session_end(a, now), a->session_pages);
        log_msg("reading paused %lld s: new session", now - a->last_turn);
        a->session_start = now;
        a->session_pages = 0;
    }
    a->last_turn = now;
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

/* ------------------------------------------------------------ receive */

static void text_right(Canvas *c, Face f, int size, int right, int base, const char *s, uint8_t ink);

/* Centred lines of text, wrapped to width w; returns the number of lines. */
static int text_centered_wrapped(Canvas *c, Face f, int size, int top, float lh, int w, const char *text, uint8_t ink)
{
    char line[512];
    const char *p = text;
    int n = 0;
    while (*p) {
        size_t best = 0, i = 0;
        for (;;) { /* the longest run of words that fits */
            while (p[i] == ' ')
                i++;
            size_t j = i;
            while (p[j] && p[j] != ' ' && p[j] != '\n')
                j++;
            if (j >= sizeof line)
                break;
            memcpy(line, p, j);
            line[j] = 0;
            if (best && font_text_width(f, size, line) > w)
                break;
            best = j;
            if (!p[j] || p[j] == '\n')
                break;
            i = j;
        }
        if (!best)
            break;
        memcpy(line, p, best);
        line[best] = 0;
        int tw = font_text_width(f, size, line);
        font_draw_text(c, f, size, 32 + (536 - tw) / 2, ui_baseline(f, size, top + n * lh, lh), line, ink, 0);
        n++;
        p += best;
        while (*p == ' ' || *p == '\n')
            p++;
    }
    return n;
}

/* ------------------------------------------------- choosing the Wi-Fi network */

static int keyboard_edit(App *a, const char *header, const char *title, const char *note, const char *msg, int strong,
                         int start_clear, char *buf, size_t n);
/* The keys start at y 350, at the bottom of the screen everywhere (Simone, 18 nov). */
static int kb_top = 350;

/* "7 · password" / "9 · password sbagliata" (the text stays, the focus starts on Cancella). */
static int password_run(App *a, const char *essid, char *buf, size_t n, int wrong)
{
    for (;;) {
        int ok = keyboard_edit(a, a->name, "Password", essid,
                               wrong ? "Password sbagliata, riprova." : "Scrivi la password della rete.", wrong, wrong,
                               buf, n);
        if (!ok)
            return 0;
        if (strlen(buf) >= 8 && strlen(buf) <= 63)
            return 1;
        /* WPA wants 8 to 63 letters: say it before trying */
        message(a, "Password troppo corta", "Le password Wi-Fi hanno almeno 8 caratteri.", REFRESH_PARTIAL);
        input_wait(2500, NULL);
    }
}

/* A centred message with an explanation, the title and the hint at the bottom (states 4, 6, 8). */
static void wifi_message(App *a, const char *title, const char *msg, const char *expl, const char *hint, int top)
{
    Canvas *c = &a->cv;
    canvas_clear(c, G_CARTA);
    ui_header(c, a->name, NULL, -1);
    if (title) /* "8 · mi collego" has none */
        ui_title(c, 48, title);
    int tw = font_text_width(FACE_SANS_MEDIUM, 24, msg);
    font_draw_text(c, FACE_SANS_MEDIUM, 24, (a->W - tw) / 2, ui_baseline(FACE_SANS_MEDIUM, 24, top, 31), msg, G_NERO,
                   a->W - 64);
    text_centered_wrapped(c, FACE_SANS, 18, top + 43, 25, 536, expl, G_NERO);
    int hw = font_text_width(FACE_MONO, 14, hint);
    font_draw_text(c, FACE_MONO, 14, (a->W - hw) / 2, ui_baseline(FACE_MONO, 14, 758, 18), hint, G_CENERE, 0);
}

/* "Lucchetto" in the Wi-Fi rows, 12.5 x 16.3: a half ring (radius 5, 1.9 px) with two legs over a
 * 12.5 x 8.8 body. */
static void lock_icon(Canvas *c, int x, int y, uint8_t ink)
{
    float cx = x + 6.3f, cy = y + 6.2f;
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 13; i++) {
            int cov = 0;
            for (int s = 0; s < 16; s++) {
                float px = x + i + (s % 4 + 0.5f) / 4, py = y + j + (s / 4 + 0.5f) / 4;
                float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
                if (py <= cy && d <= 5 && d >= 3.1f)
                    cov++;
            }
            if (cov) {
                uint8_t *p = c->px + (size_t)(y + j) * c->w + x + i;
                *p = (uint8_t)(*p + (ink - *p) * cov / 16);
            }
        }
    canvas_fill(c, x + 1, y + 6, 2, 2, ink);
    canvas_fill(c, x + 9, y + 6, 2, 2, ink);
    canvas_fill(c, x, y + 7, 13, 9, ink);
}

/* "Voce menu", Stato Wifi / Wifi Focus: the signal, the name in Medium 20, the lock when protected. */
static void wifi_row(Canvas *c, int x, int y, int w, const WifiNet *net, int focus)
{
    uint8_t ink = focus ? G_CARTA : G_NERO;
    canvas_fill(c, x, y, w, 56, focus ? G_NERO : G_CARTA);
    ui_wifi_glyph(c, x + 20, y + 21, net->bars, 0, ink, G_VELO);
    font_draw_text(c, FACE_SANS_MEDIUM, 20, x + 55, ui_baseline(FACE_SANS_MEDIUM, 20, y + 15.5f, 25), net->essid, ink,
                   w - 55 - 20 - (net->secured ? 29 : 0));
    if (net->secured)
        lock_icon(c, x + w - 20 - 13, y + 20, ink);
}

/* Connect to one network: the password keyboard when needed ("7"/"9"), then "8 · mi collego". 'title' is
 * Returns 1 once connected. */
static int wifi_join(App *a, const WifiNet *w)
{
    char pw[128] = "";
    const char *pass = w->known ? NULL : !w->secured ? "" : NULL;
    int wrong = 0, raw;
    for (;;) {
        if (!w->known && w->secured) {
            if (!password_run(a, w->essid, pw, sizeof pw, wrong))
                return 0;
            pass = pw;
        }
        if (wifi_connect(w->essid, pass) != 0) {
            memset(pw, 0, sizeof pw);
            message(a, "Non riesco a collegarmi", w->essid, REFRESH_PARTIAL);
            input_wait(3000, NULL);
            return 0;
        }
        char m[128];
        snprintf(m, sizeof m, "Mi collego a «%s»…", w->essid); /* "8 · mi collego" */
        wifi_message(a, NULL, m, "Ci vuole qualche secondo.", "indietro per annullare", 402);
        full(a, REFRESH_PARTIAL);
        int res = 0;
        while (res == 0) {
            Key kk = input_wait(500, &raw);
            if (kk == K_BACK || kk == K_HOME || kk == K_MENU || kk == K_QUIT) {
                wifi_connect_cancel();
                res = -3;
                break;
            }
            res = wifi_connect_poll();
        }
        memset(pw, 0, sizeof pw); /* the password does not stay in memory */
        ui_wifi_refresh();
        if (res == 1) {
            log_msg("wifi: connected");
            return 1;
        }
        if (res == -1 && w->secured) { /* "9 · password sbagliata": try again */
            wrong = 1;
            continue;
        }
        if (res == -2) {
            message(a, "Non riesco a collegarmi", w->essid, REFRESH_PARTIAL);
            input_wait(3000, NULL);
        }
        return 0;
    }
}

/* "5 · scegli la rete": returns 1 once connected (back to the QR), 0 to leave "Ricevi libri". */
#define WIFI_MAX 9
static int wifi_choose(App *a)
{
    WifiNet nets[WIFI_MAX];
    int n = 0, sel = 0, scan = 1;
    for (;;) {
        if (scan) { /* a new scan: the list is read 5 s later */
            wifi_radio_on();
            wifi_scan();
            wifi_message(a, "Scegli la rete Wi-Fi", "Cerco le reti…", "Ci vuole qualche secondo.",
                         "indietro per uscire", 402);
            full(a, REFRESH_PARTIAL);
            int raw;
            Key k = input_wait(g_sim ? 0 : 5000, &raw);
            if (k == K_BACK || k == K_HOME || k == K_MENU || k == K_QUIT)
                return 0;
            n = wifi_list(nets, WIFI_MAX);
            sel = 0;
            scan = 0;
            log_msg("wifi: %d networks", n);
        }
        if (n == 0) { /* "6 · nessuna rete trovata" */
            wifi_message(a, "Scegli la rete Wi-Fi", "Nessuna rete trovata", "Avvicina il Kindle al router e riprova.",
                         "centro per cercare di nuovo, indietro per uscire", 402);
            full(a, REFRESH_PARTIAL);
            int raw;
            Key k = wait_key(a, &raw);
            if (k == K_SELECT)
                scan = 1;
            else if (k == K_BACK || k == K_HOME || k == K_MENU || k == K_TIMEOUT || k == K_QUIT)
                return 0;
            continue;
        }
        /* the list: rows of 56 px from y 171, the last one "Cerca di nuovo" */
        Canvas *c = &a->cv;
        canvas_clear(c, G_CARTA);
        ui_header(c, a->name, NULL, -1);
        ui_title(c, 48, "Scegli la rete Wi-Fi");
        font_draw_text(c, FACE_SANS, 18, 32, ui_baseline(FACE_SANS, 18, 130, 25),
                       "Il Kindle non è collegato. Scegli la rete da usare per ricevere i libri.", G_NERO, 536);
        for (int i = 0; i < n; i++)
            wifi_row(c, 16, 171 + i * 56, 568, &nets[i], i == sel);
        ui_menu_row(c, 16, 171 + n * 56, 568, "Cerca di nuovo", "", ICON_ARROW, sel == n);
        const char *hint = "centro per collegarti, indietro per uscire";
        int hw = font_text_width(FACE_MONO, 14, hint);
        font_draw_text(c, FACE_MONO, 14, (a->W - hw) / 2, ui_baseline(FACE_MONO, 14, 758, 18), hint, G_CENERE, 0);
        full(a, REFRESH_PARTIAL);
        int raw;
        Key k = wait_key(a, &raw);
        if (k == K_UP)
            sel = (sel + n) % (n + 1);
        else if (k == K_DOWN)
            sel = (sel + 1) % (n + 1);
        else if (k == K_BACK || k == K_HOME || k == K_MENU || k == K_TIMEOUT || k == K_QUIT)
            return 0;
        else if (k == K_SELECT || k == K_RIGHT) {
            if (sel == n) {
                scan = 1;
                continue;
            }
            if (wifi_join(a, &nets[sel]))
                return 1;
        }
    }
}

/* Wi-Fi radio: 0 off, 1 on (in the simulator "Spegni wifi" only flips a flag). */
static int sim_wifi_off;
static int wifi_on(void)
{
    if (!g_sim)
        return rx_wifi_state() > 0;
    static int init;
    if (!init++) {
        const char *e = getenv("CARBONELLA_WIFI");
        sim_wifi_off = e && !strcmp(e, "spento");
    }
    return !sim_wifi_off;
}

/* Impostazioni › Wi-Fi (frame "04 · Impostazioni Wifi", Simone 18 nov): the "Pannello menu", 424 px wide
 * in the middle over the screen at 30%: the networks (signal, name, lock) and "Annulla". With the radio
 * off, choosing Wi-Fi switches it on and looks for the networks; with it on the first row is "Spegni wifi"
 * (Pannello menu Variant5). A network opens the password keyboard when needed, then "Mi collego"; a wrong
 * password goes back to the keyboard. */
static void wifi_panel_bg(App *a, const uint8_t *behind, int x, int y, int w, int h)
{
    for (size_t i = 0; i < (size_t)a->W * a->H; i++) /* the screen behind at 30% */
        a->cv.px[i] = (uint8_t)(behind[i] * 30 / 100);
    canvas_fill(&a->cv, x, y, w, h, G_CARTA);
    canvas_frame(&a->cv, x - 1, y - 1, w + 2, h + 2, 1, G_GRAFITE);
}

static void wifi_menu(App *a)
{
    size_t np = (size_t)a->W * a->H;
    uint8_t *behind = malloc(np);
    if (!behind)
        return;
    memcpy(behind, a->cv.px, np);
    int was_on = wifi_on();
    if (!was_on) {
        sim_wifi_off = 0;
        wifi_radio_on();
        ui_wifi_refresh();
    }
    WifiNet nets[WIFI_MAX];
    int w = 424, x = (a->W - w) / 2;
    wifi_scan();
    int n = wifi_list(nets, WIFI_MAX), connected = 0;
    if (n == 0) { /* nothing known yet: wait for the scan */
        int h = 16 + 56 + 16, y = (a->H - h) / 2;
        wifi_panel_bg(a, behind, x, y, w, h);
        ui_menu_row(&a->cv, x + 16, y + 16, w - 32, "Cerco le reti…", "", ICON_NONE, 0);
        full(a, REFRESH_PARTIAL);
        Key k = input_wait(g_sim ? 0 : 5000, NULL);
        if (k == K_BACK || k == K_HOME || k == K_MENU || k == K_QUIT)
            n = -1;
        else
            n = wifi_list(nets, WIFI_MAX);
    }
    if (n >= 0) {
        log_msg("wifi: menu, %d networks", n);
        int off_row = was_on, rows = off_row + n + 1, sel = off_row; /* focus on the first network */
        if (sel >= rows - 1)
            sel = 0;
        int h = 16 + rows * 56 + 16, y = (a->H - h) / 2;
        wifi_panel_bg(a, behind, x, y, w, h);
        int first = 1, chosen = -1;
        for (;;) {
            int r = 0;
            if (off_row)
                ui_menu_row(&a->cv, x + 16, y + 16, w - 32, "Spegni wifi", "", ICON_ARROW, sel == r++);
            for (int i = 0; i < n; i++, r++)
                wifi_row(&a->cv, x + 16, y + 16 + r * 56, w - 32, &nets[i], sel == r);
            ui_menu_row(&a->cv, x + 16, y + 16 + r * 56, w - 32, "Annulla", "", ICON_ARROW, sel == r);
            if (first)
                full(a, REFRESH_PARTIAL);
            else
                display_update(&a->cv, (Rect){ x, y, w, h }, REFRESH_PARTIAL);
            first = 0;
            Key k = wait_key(a, NULL);
            if (k == K_UP)
                sel = (sel + rows - 1) % rows;
            else if (k == K_DOWN)
                sel = (sel + 1) % rows;
            else if (k == K_SELECT || k == K_RIGHT) {
                chosen = sel;
                break;
            } else if (k == K_BACK || k == K_MENU || k == K_HOME || k == K_LEFT || k == K_TIMEOUT || k == K_QUIT)
                break;
        }
        if (chosen == 0 && off_row) { /* "Spegni wifi" */
            sim_wifi_off = 1;
            wifi_radio_off();
            ui_wifi_refresh();
            free(behind);
            return;
        }
        if (chosen >= off_row && chosen < off_row + n)
            connected = wifi_join(a, &nets[chosen - off_row]);
    }
    if (!was_on && !connected) { /* Annulla: the radio goes back off as it was */
        sim_wifi_off = 1;
        wifi_radio_off();
        ui_wifi_refresh();
    }
    free(behind);
}

typedef struct {
    char title[200], author[200];
} Arrived;

/* "Ricevi libri" (Simone, 18 nov; Figma "Ricevi libri · 1-5"): a QR code with the address of a small web
 * page; the phone sends the books there over Wi-Fi (src/ricevi.c, back end). The screen waits for the
 * Wi-Fi, then shows the code and what arrives. Returns 1 if books arrived (the Home reads the library
 * again), 2 to go back to the Kindle (timeout), 0 otherwise. */
#define RX_WIFI_WAIT_S 10 /* then the list of the networks (Simone, 18 nov) */
static int receive_run(App *a)
{
    if (rx_start(a->books, a->name) != 0) {
        message(a, "Non riesco a ricevere libri", rx_error(), REFRESH_PARTIAL);
        input_wait(4000, NULL);
        rx_stop();
        return 0;
    }
    time_t started = time(NULL);
    Arrived got[3];
    int ngot = 0, total = 0, receiving = 0, percent = 0, redraw = 2, result = 0;
    char name[256] = "";
    int sim_connected = 0;
    for (;;) {
        const char *url = rx_url()[0] ? rx_url() : rx_url_usb();
        if (g_sim && getenv("CARBONELLA_SENZA_RETE") && !sim_connected)
            url = "";
        int no_net = !url[0] && time(NULL) - started >= RX_WIFI_WAIT_S;
        if (no_net || (g_sim && !url[0])) { /* "5 · scegli la rete" */
            if (!wifi_choose(a))
                break;
            sim_connected = 1;
            started = time(NULL);
            redraw = 2;
            continue;
        }
        if (redraw) {
            Canvas *c = &a->cv;
            canvas_clear(c, G_CARTA);
            ui_header(c, a->name, NULL, -1);
            ui_title(c, 48, "Ricevi libri");
            const char *hint;
            if (!url[0]) { /* "4 · accendo il Wi-Fi" / "5 · nessuna rete" */
                const char *m = no_net ? "Nessuna rete Wi-Fi" : "Accendo il Wi-Fi…";
                const char *e = no_net ? "Il Kindle non trova una rete a cui collegarsi. Scegline una in "
                                         "Impostazioni › Wi-Fi, poi riprova."
                                       : "Ci vuole qualche secondo.";
                int top = no_net ? 389 : 402; /* 389.5 in Figma */
                int tw = font_text_width(FACE_SANS_MEDIUM, 24, m);
                font_draw_text(c, FACE_SANS_MEDIUM, 24, (a->W - tw) / 2, ui_baseline(FACE_SANS_MEDIUM, 24, top, 31), m,
                               G_NERO, 0);
                text_centered_wrapped(c, FACE_SANS, 18, top + 43, 25, 536, e, G_NERO);
                hint = no_net ? "centro per riprovare, indietro per uscire" : "indietro per annullare";
            } else {
                qr_draw(c, 140, 130, 320, url);
                int tw = font_text_width(FACE_MONO_MEDIUM, 18, url);
                font_draw_text(c, FACE_MONO_MEDIUM, 18, (a->W - tw) / 2, ui_baseline(FACE_MONO_MEDIUM, 18, 466, 23), url,
                               G_NERO, a->W - 32);
                hint = "indietro per uscire";
                if (receiving) { /* "2 · sto ricevendo" */
                    char t[300], pc[16];
                    snprintf(t, sizeof t, "Sto ricevendo «%s»", name);
                    snprintf(pc, sizeof pc, "%d%%", percent);
                    int pw = font_text_width(FACE_MONO_MEDIUM, 16, pc);
                    font_draw_text(c, FACE_SANS_MEDIUM, 20, 32, ui_baseline(FACE_SANS_MEDIUM, 20, 513, 26), t, G_NERO,
                                   536 - pw - 16);
                    text_right(c, FACE_MONO_MEDIUM, 16, 568, ui_baseline(FACE_MONO_MEDIUM, 16, 517, 21), pc, G_NERO);
                    canvas_fill(c, 32, 547, (int)lrint(536.0 * percent / 100), 14, G_NERO);
                } else if (ngot) { /* "3 · ricevuti": the last three */
                    font_draw_text(c, FACE_SANS_MEDIUM, 13, 32, ui_baseline(FACE_SANS_MEDIUM, 13, 513, 17), "ARRIVATI",
                                   G_NERO, 0);
                    for (int i = 0; i < ngot; i++) {
                        int aw = font_text_width(FACE_SANS, 16, got[i].author);
                        if (aw > 220)
                            aw = 220;
                        font_draw_text(c, FACE_SANS_MEDIUM, 18, 32, ui_baseline(FACE_SANS_MEDIUM, 18, 540 + i * 45, 23),
                                       got[i].title, G_NERO, 536 - aw - 16);
                        font_draw_text(c, FACE_SANS, 16, 568 - aw, ui_baseline(FACE_SANS, 16, 541 + i * 45, 21),
                                       got[i].author, G_NERO, 220);
                    }
                    hint = "i libri sono già nella Home · indietro per uscire";
                } else { /* "1 · in attesa" */
                    text_centered_wrapped(c, FACE_SANS, 18, 505, 25, 536,
                                          "Inquadra il codice con il telefono e scegli i libri da mandare.\n"
                                          "Il telefono deve essere sulla stessa rete Wi-Fi del Kindle.",
                                          G_NERO);
                    const char *w = "In attesa di libri…";
                    tw = font_text_width(FACE_MONO_MEDIUM, 16, w);
                    font_draw_text(c, FACE_MONO_MEDIUM, 16, (a->W - tw) / 2, ui_baseline(FACE_MONO_MEDIUM, 16, 571, 21),
                                   w, G_NERO, 0);
                }
            }
            int hw = font_text_width(FACE_MONO, 14, hint);
            font_draw_text(c, FACE_MONO, 14, (a->W - hw) / 2, ui_baseline(FACE_MONO, 14, 758, 18), hint, G_CENERE, 0);
            full(a, redraw == 2 ? REFRESH_FULL : REFRESH_PARTIAL);
            redraw = 0;
        }
        int raw;
        Key k = input_wait(150, &raw);
        if (k == K_SLEEP) { /* the server stops while the Kindle sleeps, and starts again after */
            rx_stop();
            standby(a);
            rx_start(a->books, a->name);
            started = time(NULL);
            redraw = 2;
            continue;
        }
        if (k != K_TIMEOUT && k != K_WAKE)
            touch_screensaver();
        if (k == K_BACK || k == K_HOME || k == K_MENU || k == K_QUIT)
            break;
        if (k == K_SELECT && no_net) {
            rx_stop();
            rx_start(a->books, a->name);
            started = time(NULL);
            redraw = 1;
            continue;
        }
        RxEvent ev;
        int kind;
        while (rx_step(&ev)) {
            kind = ev.kind;
            if (kind == RX_ADDRESS)
                redraw = redraw ? redraw : 1;
            else if (kind == RX_STARTED) {
                snprintf(name, sizeof name, "%s", ev.name);
                char *dot = strrchr(name, '.');
                if (dot)
                    *dot = 0;
                receiving = 1;
                percent = 0;
                redraw = redraw ? redraw : 1;
            } else if (kind == RX_PROGRESS) {
                percent = ev.percent;
                redraw = redraw ? redraw : 1;
            } else if (kind == RX_DONE) {
                receiving = 0;
                total++;
                result = 1;
                library_scan(&a->lib, a->books, a->data, a->base);
                char path[1024];
                snprintf(path, sizeof path, "%s/%s", a->books, ev.name);
                LibEntry *e = library_find(&a->lib, path);
                memmove(&got[1], &got[0], 2 * sizeof(Arrived)); /* newest first */
                snprintf(got[0].title, sizeof got[0].title, "%s", e && e->title ? e->title : name);
                snprintf(got[0].author, sizeof got[0].author, "%s", e && e->author ? e->author : "");
                if (ngot < 3)
                    ngot++;
                log_msg("received %s", ev.name);
                redraw = redraw ? redraw : 1;
            } else if (kind == RX_FAILED) {
                receiving = 0;
                message(a, "Libro non ricevuto", ev.name, REFRESH_PARTIAL);
                input_wait(3000, NULL);
                redraw = 2;
            } else if (kind == RX_IDLE) {
                log_msg("receive: idle, closing");
                goto out;
            }
        }
        if (!url[0] && !no_net && time(NULL) - started >= RX_WIFI_WAIT_S)
            redraw = 1; /* from "Accendo il Wi-Fi…" to "Nessuna rete Wi-Fi" */
        if (!url[0] && (rx_url()[0] || rx_url_usb()[0]))
            redraw = 1;
    }
out:
    rx_stop();
    log_msg("receive: %d books", total);
    return result;
}

/* ------------------------------------------------------------ settings */

static void key_test(App *a);

enum {
    S_GOALS, S_GOAL, S_YEAR, S_FONT, S_SIZE, S_LINE, S_MARGIN, S_VIEW, /* page 1, as in Figma */
    S_SHOWARCH, S_DELARCH,                                             /* page 2, v1.0.4 */
    S_NAME, S_WIFI, S_BATTPCT, S_DATE, S_STANDBY, S_INFO,
    S_N
};

typedef struct {
    int page, group_top, row_top; /* group_top: y of the group label, 0 = same group as before */
    const char *group, *label;
    int stepper; /* value shown as "‹  value  ›", changed with left/right; otherwise value + arrow */
} SetItem;

/* "04 · Impostazioni": header, title, groups (label + rows of 56 px), "Pagina 1 di 2" at the bottom.
 * Positions from the two frames: page 1 LETTURA label at 150, rows from 174; ASPETTO label at 370,
 * rows from 394. Page 2 ARCHIVIO label at 142, rows from 166; SISTEMA label at 298, rows from 322 (18 nov).
 * Texts exactly as in the frames. */
static const SetItem SET[S_N] = {
    { 0, 150, 174, "Lettura", "Guarda gli obiettivi", 0 }, /* texts verbatim from Figma */
    { 0, 0, 230, NULL, "Obiettivo giornaliero", 1 },
    { 0, 0, 286, NULL, "Libri da leggere nell’anno", 1 },
    { 0, 370, 394, "Aspetto", "Carattere", 1 },
    { 0, 0, 450, NULL, "Dimensione", 1 },
    { 0, 0, 506, NULL, "Interlinea", 1 },
    { 0, 0, 562, NULL, "Margini", 1 },
    { 0, 0, 618, NULL, "Visualizzazione", 1 },
    { 1, 142, 166, "Archivio", "Mostra archivio", 0 }, /* Simone, 18 nov: "Mostra sottolineature" out */
    { 1, 0, 222, NULL, "Elimina archivio definitivamente", 0 },
    { 1, 298, 322, "Sistema", "Modifica nome dispositivo", 0 },
    { 1, 0, 378, NULL, "Wi-Fi", 0 },
    { 1, 0, 434, NULL, "Mostra percentuale batteria", 1 }, /* Simone, 18 nov */
    { 1, 0, 490, NULL, "Data e ora", 0 },
    { 1, 0, 546, NULL, "Standby automatico dopo", 1 }, /* v0.6 */
    { 1, 0, 602, NULL, "Informazioni", 0 },
};

static void set_value(App *a, int i, char *out, size_t n)
{
    out[0] = 0;
    switch (i) {
    case S_GOAL: snprintf(out, n, "%d m", a->goal_min); break;
    case S_YEAR: snprintf(out, n, "%d", a->books_year); break;
    case S_FONT: snprintf(out, n, "Literata"); break;
    case S_SIZE: snprintf(out, n, "%d pt", a->lp.size); break;
    case S_LINE: snprintf(out, n, "%d,%d pt", a->lp.line_pct / 100, a->lp.line_pct % 100 / 10); break;
    case S_MARGIN: snprintf(out, n, "%s", MARGIN_NAMES[closest(MARGINS, 3, a->lp.margin_x)]); break;
    case S_VIEW: snprintf(out, n, "Copertine"); break;
    case S_NAME: snprintf(out, n, "%s", a->name); break;
    case S_WIFI: snprintf(out, n, "%s", wifi_on() ? "Acceso" : "Spento"); break;
    case S_DATE: {
        static const char *MESI[] = { "gen", "feb", "mar", "apr", "mag", "giu",
                                      "lug", "ago", "set", "ott", "nov", "dic" };
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(out, n, "%d %s %d", tm.tm_mday, MESI[tm.tm_mon], tm.tm_year + 1900);
        break;
    }
    case S_STANDBY: snprintf(out, n, "%d m", a->standby_min); break;
    case S_BATTPCT: snprintf(out, n, "%s", g_battery_pct ? "Sì" : "No"); break;
    case S_INFO: snprintf(out, n, "v%s", CARBONELLA_VERSION); break;
    default: break;
    }
}

static void set_row(App *a, int i, int focus)
{
    char v[160], w[180];
    set_value(a, i, v, sizeof v);
    if (SET[i].stepper) { /* as in Figma: "‹  30 m  ›", no arrow */
        snprintf(w, sizeof w, "\xE2\x80\xB9  %s  \xE2\x80\xBA", v);
        ui_menu_row(&a->cv, 32, SET[i].row_top, a->W - 64, SET[i].label, w, ICON_NONE, focus);
    } else /* Simone, 18 nov: Informazioni is only a value, no arrow */
        ui_menu_row(&a->cv, 32, SET[i].row_top, a->W - 64, SET[i].label, v, i == S_INFO ? ICON_NONE : ICON_ARROW, focus);
}

/* Change a setting by one step (dir +1 / -1). Returns 1 if the reading page must be laid out again. */
static int set_change(App *a, int i, int dir)
{
    switch (i) {
    case S_GOAL: a->goal_min = step(GOALS, NGOALS, a->goal_min, dir); break;
    case S_YEAR: a->books_year = year_step(a->books_year, dir); break;
    case S_STANDBY: a->standby_min = step(STANDBYS, NSTANDBYS, a->standby_min, dir); break;
    case S_BATTPCT: g_battery_pct = !g_battery_pct; break;
    case S_SIZE: a->lp.size = step(SIZES, NSIZES, a->lp.size, dir); break;
    case S_LINE: a->lp.line_pct = step(LINES, NLINES, a->lp.line_pct, dir); break;
    case S_MARGIN: a->lp.margin_x = step(MARGINS, 3, a->lp.margin_x, dir); break;
    default: return 0; /* Carattere and Visualizzazione have one choice for now */
    }
    save_prefs(a);
    return i == S_SIZE || i == S_LINE || i == S_MARGIN;
}

/* "Pagina N di M" at the bottom of a full screen, as in the Footer of Figma. */
static void page_footer(App *a, const char *text)
{
    int tw = font_text_width(FACE_MONO, 16, text);
    font_draw_text(&a->cv, FACE_MONO, 16, (a->W - tw) / 2, ui_baseline(FACE_MONO, 16, a->H - 8 - 19.2f, 19.2f), text,
                   G_CENERE, 0);
}

static void settings_draw(App *a, int page, int sel)
{
    canvas_clear(&a->cv, G_CARTA);
    ui_header(&a->cv, a->name, NULL, -1); /* as in the frame: no minutes in the header here */
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
    page_footer(a, pg);
}

/* The settings screen. Up/down move, centre and right go to the next value, left to the previous one,
 * Back closes. Returns 1 to rescan the books, 2 to go back to the Kindle (timeout), 3 to show the
 * archive, 4 to open the highlights, 0 otherwise. */
static int goals_run(App *a);
static LibEntry *clippings_entry(App *a);
static int delete_archive(App *a);

static int settings_run(App *a)
{
    int sel = 0, shown = -1, relaid = 0, result = 0;
    for (;;) {
        int page = SET[sel].page;
        if (page != shown) {
            settings_draw(a, page, sel);
            full(a, REFRESH_FULL);
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
            sel = page == 0 ? S_SHOWARCH : 0;
        } else if (k == K_NEXT) {
            sel = page == 0 ? S_SHOWARCH : 0;
        } else if (k == K_SELECT || k == K_RIGHT || k == K_LEFT) {
            if (sel == S_SHOWARCH && (k == K_SELECT || k == K_RIGHT)) {
                result = 3; /* the grid of the archive opens from the Home */
                break;
            }
            if (sel == S_DELARCH && (k == K_SELECT || k == K_RIGHT)) {
                if (confirm_popup(a, "Sei sicuro di voler eliminare l'archivio?")) {
                    int nd = delete_archive(a);
                    char t[64];
                    snprintf(t, sizeof t, nd == 1 ? "%d libro eliminato" : "%d libri eliminati", nd);
                    message(a, "Archivio eliminato", t, REFRESH_PARTIAL);
                    input_wait(2000, NULL);
                    result = 1;
                }
                shown = -1;
                continue;
            }
            if (sel == S_WIFI) { /* the Wi-Fi menu (Simone, 18 nov) */
                if (k != K_SELECT && k != K_RIGHT)
                    continue;
                wifi_menu(a);
                shown = -1;
                continue;
            }
            if (sel == S_GOALS) {
                if (k != K_SELECT && k != K_RIGHT)
                    continue;
                if (goals_run(a) == 2) {
                    result = 2;
                    break;
                }
                shown = -1; /* draw the settings again */
                continue;
            }
            relaid |= set_change(a, sel, k == K_LEFT ? -1 : 1);
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

/* ------------------------------------------------------------ goals */

/* Reading history, from the sessions in letture.tsv: seconds read per day for the last HIST_DAYS days
 * (index 0 = today). */
#define HIST_DAYS 800
typedef struct {
    int secs[HIST_DAYS];
} History;

static long day_number(long long t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    return (long)((t + tm.tm_gmtoff) / 86400);
}

static void history_add(History *h, long today, long long st, long long en)
{
    if (en <= st || en - st > 12 * 3600)
        return;
    while (st < en) {
        long d = day_number(st);
        time_t tt = (time_t)st;
        struct tm tm;
        localtime_r(&tt, &tm);
        long long end_of_day = st + (86400 - (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec));
        long long e = en < end_of_day ? en : end_of_day;
        long i = today - d;
        if (i >= 0 && i < HIST_DAYS)
            h->secs[i] += (int)(e - st);
        st = e;
    }
}

static void history_load(App *a, History *h)
{
    memset(h, 0, sizeof *h);
    long long now = time(NULL);
    long today = day_number(now);
    if (g_sim) { /* as in the Figma frame: goal reached most days, 18 minutes today */
        for (int i = 1; i < 40; i++)
            h->secs[i] = (i % 7 == 3 || i == 15) ? 12 * 60 : 35 * 60;
        h->secs[0] = 18 * 60;
        return;
    }
    char path[700];
    snprintf(path, sizeof path, "%s/letture.tsv", a->data);
    char *s = read_file(path, NULL);
    for (char *p = s; p && *p;) {
        long long st = 0, en = 0;
        if (sscanf(p, "%lld\t%lld", &st, &en) == 2)
            history_add(h, today, st, en);
        char *nl = strchr(p, '\n');
        if (!nl)
            break;
        p = nl + 1;
    }
    free(s);
    if (a->book && a->session_start)
        history_add(h, today, a->session_start, session_end(a, now));
}

static void bar(Canvas *c, int x, int y, int w, double frac)
{
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    canvas_fill(c, x, y, w, 8, G_CARTA);
    canvas_fill(c, x, y, (int)(w * frac + 0.5), 8, G_NERO);
    canvas_frame(c, x, y, w, 8, 2, G_NERO);
}

static void text_right(Canvas *c, Face f, int size, int right, int base, const char *s, uint8_t ink)
{
    font_draw_text(c, f, size, right - font_text_width(f, size, s), base, s, ink, 0);
}

/* Books finished this year: the ones at 100% whose last reading is in this year. */
static int books_read_this_year(App *a)
{
    if (g_sim)
        return 3; /* as in the frame */
    time_t now = time(NULL);
    struct tm tn;
    localtime_r(&now, &tn);
    int n = 0;
    for (int i = 0; i < a->lib.n; i++) {
        const LibEntry *e = &a->lib.e[i];
        if (e->percent < 100 || e->last_read <= 0)
            continue;
        time_t t = (time_t)e->last_read;
        struct tm tm;
        localtime_r(&t, &tm);
        n += tm.tm_year == tn.tm_year;
    }
    return n;
}

enum { G_ROW_GOAL, G_ROW_YEAR, G_ROWS };
#define GOALS_ROW_TOP 595

static void goals_row(App *a, int i, int focus)
{
    char v[64];
    if (i == G_ROW_GOAL)
        snprintf(v, sizeof v, "\xE2\x80\xB9  %d m  \xE2\x80\xBA", a->goal_min);
    else
        snprintf(v, sizeof v, "\xE2\x80\xB9  %d  \xE2\x80\xBA", a->books_year);
    ui_menu_row(&a->cv, 32, GOALS_ROW_TOP + i * 56, a->W - 64,
                i == G_ROW_GOAL ? "Obiettivo giornaliero" : "Libri da leggere nell’anno", v, ICON_NONE, focus);
}

/* "06 · Obiettivi" as redrawn by Simone on 17 and 18 nov (Figma 102:2118): today with this week's total and a
 * thin bar, the month as numbered cells, series and record, this year's books as numbered boxes, and
 * the two goals to set. Texts, positions and greys from the frame. */
#define INK_333 204 /* #333 captions */
static int centered_x(Face f, int size, int x, int w, const char *s)
{
    return x + (w - font_text_width(f, size, s)) / 2;
}

static void goals_draw(App *a, int sel)
{
    static const char *GIORNI[] = { "DOM", "LUN", "MAR", "MER", "GIO", "VEN", "SAB" };
    static const char *MESI[] = { "GEN", "FEB", "MAR", "APR", "MAG", "GIU", "LUG", "AGO", "SET", "OTT", "NOV", "DIC" };
    static const char *MESI_LUNGHI[] = { "GENNAIO", "FEBBRAIO", "MARZO",     "APRILE",  "MAGGIO",   "GIUGNO",
                                         "LUGLIO",  "AGOSTO",   "SETTEMBRE", "OTTOBRE", "NOVEMBRE", "DICEMBRE" };
    static const char *LETTERE[] = { "L", "M", "M", "G", "V", "S", "D" };
    Canvas *c = &a->cv;
    History *h = xmalloc(sizeof *h);
    history_load(a, h);
    time_t now = time(NULL);
    struct tm tn;
    localtime_r(&now, &tn);
    int goal_s = a->goal_min * 60;
    char buf[128];

    canvas_clear(c, G_CARTA);
    ui_header(c, "Obiettivi", NULL, -1);

    /* Oggi: "18 min" on the left, "questa settimana 1h30m" on the right, then the bar */
    snprintf(buf, sizeof buf, "OGGI \xC2\xB7 %s %d %s", GIORNI[tn.tm_wday], tn.tm_mday, MESI[tn.tm_mon]);
    font_draw_text(c, FACE_SANS_MEDIUM, 13, 32, ui_baseline(FACE_SANS_MEDIUM, 13, 80, 17), buf, G_CENERE, 0);
    int today_min = h->secs[0] / 60;
    snprintf(buf, sizeof buf, "%d min", today_min);
    font_draw_text(c, FACE_SANS_MEDIUM, 32, 32, ui_baseline(FACE_SANS_MEDIUM, 32, 97, 42), buf, G_NERO, 0);
    int wd = (tn.tm_wday + 6) % 7; /* 0 = Monday */
    long week = 0;
    for (int i = 0; i <= wd; i++)
        week += h->secs[i];
    if (week >= 3600)
        snprintf(buf, sizeof buf, "%ldh%02ldm", week / 3600, week % 3600 / 60);
    else
        snprintf(buf, sizeof buf, "%ldm", week / 60);
    int vw = font_text_width(FACE_SANS_MEDIUM, 30, buf);
    font_draw_text(c, FACE_SANS_MEDIUM, 30, 568 - vw, ui_baseline(FACE_SANS_MEDIUM, 30, 99, 39), buf, G_NERO, 0);
    text_right(c, FACE_MONO, 13, 568 - vw - 8, ui_baseline(FACE_MONO, 13, 117, 17), "questa settimana", INK_333);
    double f = a->goal_min > 0 ? (double)today_min / a->goal_min : 0;
    if (f > 1)
        f = 1;
    canvas_fill(c, 32, 147, 536, 3, G_VELO);
    canvas_fill(c, 32, 147, (int)(536 * f + 0.5), 3, G_GRAFITE);

    /* calendar of the month: numbered cells 68 x 24 */
    snprintf(buf, sizeof buf, "%s %d", MESI_LUNGHI[tn.tm_mon], tn.tm_year + 1900);
    font_draw_text(c, FACE_SANS_MEDIUM, 13, 32, ui_baseline(FACE_SANS_MEDIUM, 13, 158, 17), buf, G_CENERE, 0);
    for (int i = 0; i < 7; i++)
        font_draw_text(c, FACE_MONO_MEDIUM, 13, centered_x(FACE_MONO_MEDIUM, 13, (int)lrintf(32 + i * 77.7f), 70, LETTERE[i]),
                       ui_baseline(FACE_MONO_MEDIUM, 13, 183, 17), LETTERE[i], G_CENERE, 0);
    struct tm first = tn;
    first.tm_mday = 1;
    first.tm_hour = 12;
    mktime(&first);
    int off = (first.tm_wday + 6) % 7;
    static const int MDAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int y = tn.tm_year + 1900;
    int ndays = MDAYS[tn.tm_mon] + (tn.tm_mon == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    for (int d = 1; d <= ndays; d++) {
        int cell = off + d - 1;
        int x = (int)lrintf(32 + (cell % 7) * 77.7f), cy = 209 + (cell / 7) * 29; /* cells 70 x 23 */
        int ago = tn.tm_mday - d;
        int sct = ago >= 0 && ago < HIST_DAYS ? h->secs[ago] : 0;
        uint8_t fill = G_CARTA, ink = G_CENERE; /* not read, or still to come */
        if (ago >= 0 && goal_s > 0 && sct >= goal_s) { /* goal reached */
            fill = G_CENERE;
            ink = G_CARTA;
        } else if (ago >= 0 && sct > 0) /* read less */
            fill = G_LEGGERO;
        canvas_fill(c, x, cy, 70, 23, fill);
        if (ago == 0) /* today */
            canvas_frame(c, x, cy, 70, 23, 2, G_CENERE);
        snprintf(buf, sizeof buf, "%d", d);
        font_draw_text(c, FACE_MONO_MEDIUM, 13, centered_x(FACE_MONO_MEDIUM, 13, x, 70, buf),
                       ui_baseline(FACE_MONO_MEDIUM, 13, cy + 3, 17), buf, ink, 0);
    }

    /* series and record: days in a row with the goal reached (today counts once reached) */
    int streak = 0, record = 0, run = 0;
    for (int i = h->secs[0] >= goal_s ? 0 : 1; i < HIST_DAYS && h->secs[i] >= goal_s && goal_s > 0; i++)
        streak++;
    for (int i = HIST_DAYS - 1; i >= 0; i--) {
        run = goal_s > 0 && h->secs[i] >= goal_s ? run + 1 : 0;
        if (run > record)
            record = run;
    }
    const int stat_x[2] = { 32, 216 };
    const int stat_v[2] = { streak, record };
    const char *stat_l[2] = { streak == 1 ? "giorno di fila" : "giorni di fila", "record giorni di fila" };
    for (int i = 0; i < 2; i++) {
        snprintf(buf, sizeof buf, "%d", stat_v[i]);
        font_draw_text(c, FACE_SANS_MEDIUM, 30, stat_x[i], ui_baseline(FACE_SANS_MEDIUM, 30, 377, 39), buf, G_NERO, 0);
        int w = font_text_width(FACE_SANS_MEDIUM, 30, buf);
        font_draw_text(c, FACE_MONO, 13, stat_x[i] + w + 8, ui_baseline(FACE_MONO, 13, 395, 17), stat_l[i], INK_333, 0);
    }
    canvas_fill(c, 32, 424, 536, 1, G_CENERE); /* Simone, 18 nov: 1 px #666 */

    /* this year: the books as numbered boxes, the ones read first */
    font_draw_text(c, FACE_SANS_MEDIUM, 13, 32, ui_baseline(FACE_SANS_MEDIUM, 13, 433, 17), "QUEST\xE2\x80\x99" "ANNO",
                   G_CENERE, 0);
    int read = books_read_this_year(a);
    snprintf(buf, sizeof buf, "%d libr%s lett%s di %d", read, read == 1 ? "o" : "i", read == 1 ? "o" : "i",
             a->books_year);
    font_draw_text(c, FACE_SANS_MEDIUM, 20, 32, ui_baseline(FACE_SANS_MEDIUM, 20, 453, 26), buf, G_NERO, 0);
    int ydays = 365 + ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
    double expected = (double)a->books_year * (tn.tm_yday + 1) / ydays;
    text_right(c, FACE_MONO_MEDIUM, 16, 568, ui_baseline(FACE_MONO_MEDIUM, 16, 457, 21),
               read + 0.5 >= expected ? "in linea" : "in ritardo", G_NERO);
    int nb = a->books_year > 0 ? a->books_year : 0;
    int cols = nb <= 12 ? 6 : (nb + 1) / 2; /* two rows, as in the frame; more books = narrower boxes */
    float pitch = cols > 1 ? (536.0f - 83.0f * 6 / cols) / (cols - 1) : 0;
    int bw = (int)lrintf(83.0f * 6 / cols);
    if (cols > 6) {
        pitch = (536.0f + 7) / cols;
        bw = (int)(pitch - 7);
    }
    for (int i = 0; i < nb; i++) {
        int bx = (int)lrintf(32 + (i % cols) * pitch), by = 487 + (i / cols) * 32;
        int done = i < read;
        canvas_fill(c, bx, by, bw, 24, done ? G_CENERE : G_LEGGERO);
        snprintf(buf, sizeof buf, "%d", i + 1);
        if (font_text_width(FACE_MONO_MEDIUM, 13, buf) <= bw - 2)
            font_draw_text(c, FACE_MONO_MEDIUM, 13, centered_x(FACE_MONO_MEDIUM, 13, bx, bw, buf),
                           ui_baseline(FACE_MONO_MEDIUM, 13, by + 3.5f, 17), buf, done ? G_CARTA : G_CENERE, 0);
    }

    /* Imposta */
    ui_group_label(c, 52, 571, "Imposta");
    for (int i = 0; i < G_ROWS; i++)
        goals_row(a, i, i == sel);
    free(h); /* no footer, as in the frame */
}

/* The goals screen. Up/down move between the two goals, left/right (and centre) change them.
 * Returns 2 to go back to the Kindle (timeout), 0 otherwise. */
static int goals_run(App *a)
{
    int sel = 0;
    goals_draw(a, sel);
    full(a, REFRESH_FULL);
    for (;;) {
        int raw;
        Key k = wait_key(a, &raw);
        if (k == K_NONE)
            continue;
        if (k == K_UP || k == K_DOWN) {
            int old = sel;
            sel = (sel + 1) % G_ROWS;
            goals_row(a, old, 0);
            goals_row(a, sel, 1);
            display_update(&a->cv, (Rect){ 32, GOALS_ROW_TOP, a->W - 64, 112 }, REFRESH_PARTIAL);
        } else if (k == K_LEFT || k == K_RIGHT || k == K_SELECT) {
            int dir = k == K_LEFT ? -1 : 1;
            if (sel == G_ROW_GOAL)
                a->goal_min = step(GOALS, NGOALS, a->goal_min, dir);
            else
                a->books_year = year_step(a->books_year, dir);
            save_prefs(a);
            goals_draw(a, sel); /* bars, calendar and year depend on the goals */
            full(a, REFRESH_PARTIAL);
        } else if (k == K_BACK || k == K_MENU || k == K_HOME)
            return 0;
        else if (k == K_TIMEOUT || k == K_QUIT)
            return 2;
    }
}

/* ------------------------------------------------------------ keyboard */

/* "Tastiera" (Figma 136:2828, Simone 18 nov), shared by Cerca nel libro, Crea nuova raccolta and the
 * Wi-Fi password: keys 50 x 56 every 54 px from x 32, rows every 64 px from y 216; ⇪ caps lock, ⇧ one
 * capital, ⌫ one letter back, #+= the symbols (then "abc"); Cancella (empties the field) and Fatto
 * below at y 552, 266 px each. */
enum { KK_CHAR, KK_CAPS, KK_SHIFT, KK_BKSP, KK_SYM, KK_SPACE, KK_CLEAR, KK_DONE };
typedef struct {
    int x, w, kind;
    const char *label;
} KbKey;
#define KB_NROWS 6
static int kb_row(int r, int mode, KbKey *out)
{
    /* mode: 0 lower, 1 one capital, 2 caps lock, 3 symbols */
    static const char *D = "1234567890";
    static const char *LOW[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" }, *UP[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    static const char *SYM1[10] = { "!", "@", "#", "$", "%", "&", "*", "(", ")", "?" };
    static const char *SYM2[9] = { "_", "+", "=", "/", "\\", ":", ";", "\"", "~" };
    static const char *SYM3[7] = { "à", "è", "é", "ì", "ò", "ù", "€" };
    static char one[3][10][2];
    int n = 0, x = 32;
#define ADD(W, K, L) (out[n++] = (KbKey){ x, (W), (K), (L) }, x += (W) + 4)
    if (r == 0) {
        for (int i = 0; i < 10; i++) {
            one[0][i][0] = D[i];
            ADD(50, KK_CHAR, one[0][i]);
        }
    } else if (r == 1) {
        for (int i = 0; i < 10; i++) {
            if (mode == 3) {
                ADD(50, KK_CHAR, SYM1[i]);
            } else {
                one[1][i][0] = (mode ? UP : LOW)[0][i];
                ADD(50, KK_CHAR, one[1][i]);
            }
        }
    } else if (r == 2) {
        ADD(50, KK_CAPS, "");
        for (int i = 0; i < 9; i++) {
            if (mode == 3) {
                ADD(50, KK_CHAR, SYM2[i]);
            } else {
                one[2][i][0] = (mode ? UP : LOW)[1][i];
                ADD(50, KK_CHAR, one[2][i]);
            }
        }
    } else if (r == 3) {
        static char three[7][2];
        ADD(77, KK_SHIFT, "");
        for (int i = 0; i < 7; i++) {
            if (mode == 3) {
                ADD(50, KK_CHAR, SYM3[i]);
            } else {
                three[i][0] = (mode ? UP : LOW)[2][i];
                ADD(50, KK_CHAR, three[i]);
            }
        }
        ADD(77, KK_BKSP, "");
    } else if (r == 4) {
        ADD(104, KK_SYM, mode == 3 ? "abc" : "#+=");
        ADD(50, KK_CHAR, "'");
        ADD(212, KK_SPACE, "Spazio");
        ADD(50, KK_CHAR, ",");
        ADD(50, KK_CHAR, ".");
        ADD(50, KK_CHAR, "-");
    } else {
        ADD(266, KK_CLEAR, "Cancella");
        ADD(266, KK_DONE, "Fatto");
    }
#undef ADD
    return n;
}

static int kb_row_y(int r) { return r < 5 ? kb_top + r * 64 : kb_top + 336; }

static void kb_draw_one(App *a, int r, const KbKey *k, int focus, int active)
{
    Canvas *c = &a->cv;
    int y = kb_row_y(r);
    uint8_t ink = focus ? G_CARTA : G_NERO;
    canvas_fill(c, k->x, y, k->w, 56, focus ? G_NERO : G_CARTA);
    if (!focus)
        canvas_frame(c, k->x, y, k->w, 56, active ? 4 : 2, G_NERO);
    float ox = k->x + (k->w - 16) / 2.0f;
    if (k->kind == KK_SHIFT || k->kind == KK_CAPS) { /* arrow, filled when active; caps has a bar under it */
        float h = k->kind == KK_SHIFT ? 9 : 8, b = k->kind == KK_SHIFT ? 16 : 12.5f, oy = y + (k->kind == KK_SHIFT ? 20 : 19);
        float pts[16] = { ox + 8, oy, ox + 16, oy + h, ox + 11.5f, oy + h, ox + 11.5f, oy + b, ox + 4.5f, oy + b,
                          ox + 4.5f, oy + h, ox, oy + h, ox + 8, oy };
        if (active && !focus)
            canvas_polygon(c, pts, 7, ink);
        canvas_polyline(c, pts, 8, 2, ink);
        if (k->kind == KK_CAPS)
            canvas_fill(c, (int)lrintf(ox + 4.5f), y + 34, 7, 3, ink);
        return;
    }
    if (k->kind == KK_BKSP) { /* a key shape with a cross */
        float bx = k->x + (k->w - 24) / 2.0f, by = y + 21;
        float pts[12] = { bx + 7, by, bx + 24, by, bx + 24, by + 14, bx + 7, by + 14, bx, by + 7, bx + 7, by };
        canvas_polyline(c, pts, 6, 2, ink);
        float x1[4] = { bx + 11.5f, by + 4, bx + 17.5f, by + 10 }, x2[4] = { bx + 17.5f, by + 4, bx + 11.5f, by + 10 };
        canvas_polyline(c, x1, 2, 2, ink);
        canvas_polyline(c, x2, 2, 2, ink);
        return;
    }
    int big = k->kind == KK_CHAR;
    Face f = big ? FACE_SANS : FACE_SANS_MEDIUM;
    int size = big ? 24 : 18;
    int tw = font_text_width(f, size, k->label);
    font_draw_text(c, f, size, k->x + (k->w - tw) / 2, ui_baseline(f, size, y + 12.5f, 31), k->label, ink, 0);
}

static void kb_draw_field(App *a, const char *buf)
{
    canvas_fill(&a->cv, 32, 128, 536, 56, G_CARTA);
    canvas_frame(&a->cv, 32, 128, 536, 56, 2, G_NERO);
    int tw = font_text_width(FACE_SANS, 20, buf);
    int x0 = 50, maxw = 536 - 40;
    if (tw > maxw) /* long text: show the end */
        x0 -= tw - maxw;
    canvas_clip(&a->cv, 34, 130, 532, 52);
    int end = font_draw_text(&a->cv, FACE_SANS, 20, x0, ui_baseline(FACE_SANS, 20, 143, 26), buf, G_NERO, 0);
    canvas_noclip(&a->cv);
    canvas_fill(&a->cv, end + 2, 142, 2, 28, G_NERO); /* cursor */
}

/* The keyboard screen. note: small text right of the title (the Wi-Fi network), msg: the line under the
 * field (medium when 'strong'), start_clear: focus on Cancella (wrong password). Returns 1 with the text
 * in buf, 0 if cancelled. */
static int keyboard_edit(App *a, const char *header, const char *title, const char *note, const char *msg, int strong,
                         int start_clear, char *buf, size_t n)
{
    int mode = 0, r = start_clear ? 5 : 1, c = 0, redraw = 2;
    KbKey row[12];
    for (;;) {
        if (redraw) {
            Canvas *cv = &a->cv;
            canvas_clear(cv, G_CARTA);
            ui_header(cv, header, NULL, -1);
            ui_title(cv, 48, title);
            if (note)
                text_right(cv, FACE_SANS, 16, 568, ui_baseline(FACE_SANS, 16, 83, 21), note, G_CENERE);
            kb_draw_field(a, buf);
            if (msg)
                font_draw_text(cv, strong ? FACE_SANS_MEDIUM : FACE_SANS, 16, 32, ui_baseline(FACE_SANS, 16, 190, 20),
                               msg, G_NERO, 536);
            for (int i = 0; i < KB_NROWS; i++) {
                int nk = kb_row(i, mode, row);
                for (int j = 0; j < nk; j++) {
                    int act = (row[j].kind == KK_SHIFT && mode == 1) || (row[j].kind == KK_CAPS && mode == 2);
                    kb_draw_one(a, i, &row[j], i == r && j == c, act);
                }
            }
            const char *hint = "Frecce per scegliere, centro per scrivere, Indietro per annullare";
            int hw = font_text_width(FACE_MONO, 14, hint);
            font_draw_text(cv, FACE_MONO, 14, (a->W - hw) / 2, ui_baseline(FACE_MONO, 14, 758, 18), hint, G_CENERE,
                           a->W - 32);
            full(a, redraw == 2 ? REFRESH_FULL : REFRESH_PARTIAL);
            redraw = 0;
        }
        int raw;
        Key k = wait_key(a, &raw);
        int nk = kb_row(r, mode, row);
        switch (k) {
        case K_LEFT: c = (c + nk - 1) % nk; break;
        case K_RIGHT: c = (c + 1) % nk; break;
        case K_UP:
        case K_DOWN: { /* the key of the next row nearest to the middle of this one */
            int mid = row[c].x + row[c].w / 2;
            r = (r + (k == K_DOWN ? 1 : KB_NROWS - 1)) % KB_NROWS;
            KbKey nr[12];
            int m = kb_row(r, mode, nr), best = 0, bd = 1 << 30;
            for (int j = 0; j < m; j++) {
                int d = abs(nr[j].x + nr[j].w / 2 - mid);
                if (d < bd) {
                    bd = d;
                    best = j;
                }
            }
            c = best;
            break;
        }
        case K_SELECT: {
            size_t l = strlen(buf);
            const KbKey *key = &row[c];
            switch (key->kind) {
            case KK_CHAR:
                if (l + strlen(key->label) < n)
                    strcat(buf, key->label);
                if (mode == 1)
                    mode = 0; /* ⇧: one capital only */
                break;
            case KK_SPACE:
                if (l + 1 < n)
                    strcat(buf, " ");
                break;
            case KK_BKSP:
                while (l > 0 && ((unsigned char)buf[l - 1] & 0xC0) == 0x80) /* whole UTF-8 letter */
                    l--;
                if (l > 0)
                    buf[l - 1] = 0;
                break;
            case KK_CLEAR: buf[0] = 0; break;
            case KK_SHIFT: mode = mode == 1 ? 0 : 1; break;
            case KK_CAPS: mode = mode == 2 ? 0 : 2; break;
            case KK_SYM: mode = mode == 3 ? 0 : 3; break;
            case KK_DONE: return 1;
            }
            msg = msg && strong ? NULL : msg; /* the "wrong password" line goes once he types */
            break;
        }
        case K_BACK:
        case K_HOME:
        case K_MENU:
        case K_TIMEOUT:
        case K_QUIT: return 0;
        default: continue;
        }
        nk = kb_row(r, mode, row);
        if (c >= nk)
            c = nk - 1;
        redraw = 1;
    }
}

static int keyboard_run(App *a, const char *header, const char *title, char *buf, size_t n)
{
    return keyboard_edit(a, header, title, NULL, NULL, 0, 0, buf, n);
}

/* ------------------------------------------------- pages of the whole book */

static uint64_t spine_bytes(App *a, int from, int to)
{
    uint64_t b = 0;
    for (int i = from; i < to && i < a->book->nspine; i++)
        b += a->book->spine_size[i];
    return b;
}

/* First page (1-based, whole book) of spine item s, with the same estimate as book_pages():
 * the open chapter counts with its real pages, the others from their size. */
static int spine_first_page(App *a, int s)
{
    double bpp = bytes_per_page(a);
    if (s <= a->spine)
        return (int)(spine_bytes(a, 0, s) / bpp + 0.5) + 1;
    return (int)(spine_bytes(a, 0, a->spine) / bpp + 0.5) + a->pg.n +
           (int)(spine_bytes(a, a->spine + 1, s) / bpp + 0.5) + 1;
}

static int spine_pages(App *a, int s)
{
    if (s == a->spine)
        return a->pg.n > 0 ? a->pg.n : 1;
    int n = (int)(a->book->spine_size[s] / bytes_per_page(a) + 0.5);
    return n > 0 ? n : 1;
}

/* Book page of byte 'pos' of the text of spine item s (text_len = length of that chapter text). */
static int page_of_pos(App *a, int s, uint32_t pos, uint32_t text_len)
{
    if (s == a->spine && a->pg.n > 0)
        return spine_first_page(a, s) + pagination_find(&a->pg, pos);
    int n = spine_pages(a, s);
    int in = text_len ? (int)((double)pos / text_len * n) : 0;
    return spine_first_page(a, s) + (in < n ? in : n - 1);
}

/* Goes to page p of the whole book (as shown in "pag. 12 di 684"). */
static void goto_page(App *a, int p)
{
    int s = 0;
    while (s + 1 < a->book->nspine && spine_first_page(a, s + 1) <= p)
        s++;
    if (s != a->spine && load_chapter(a, s, 0, 0) != 0)
        return;
    int in = p - spine_first_page(a, s); /* again: the chapter is now the open one */
    if (in < 0)
        in = 0;
    if (in >= a->pg.n)
        in = a->pg.n - 1;
    a->page = in;
    save_position(a);
}

/* "Pagine del cartaceo": the pages of the paper edition, shown on the Home, written by hand (Simone,
 * 17 nov). Same keys as "Vai a pagina…"; 0 = not known (the value of the table or of the EPUB comes
 * back at the next reading of the library). */
#define INK_555 170 /* #555 hints */
static int print_pages_run(App *a)
{
    LibEntry *e = a->entry;
    int p = e->print_pages > 0 ? e->print_pages : e->pages > 0 ? e->pages : 0; /* start near the screen pages */
    int first = 1;
    for (;;) {
        /* "02 · Lettura · Pagine del cartaceo" (Simone, 18 nov): "< 158 >" Plex Mono Medium 64 centred,
         * box top 232; caption Mono 18 black at 319; four hints Mono 14 #555 from 406, every 26 px */
        canvas_clear(&a->cv, G_CARTA);
        ui_header(&a->cv, a->book->title, NULL, -1);
        ui_title(&a->cv, 48, "Pagine del cartaceo");
        char num[24];
        snprintf(num, sizeof num, "< %d >", p);
        const char *of = p > 0 ? "pagine sulla Home" : "non note: sulla Home le pagine a schermo";
        int tw = font_text_width(FACE_MONO_MEDIUM, 64, num);
        font_draw_text(&a->cv, FACE_MONO_MEDIUM, 64, (a->W - tw) / 2, ui_baseline(FACE_MONO_MEDIUM, 64, 232, 83), num,
                       G_NERO, 0);
        tw = font_text_width(FACE_MONO, 18, of);
        font_draw_text(&a->cv, FACE_MONO, 18, (a->W - tw) / 2, ui_baseline(FACE_MONO, 18, 319, 23), of, G_NERO,
                       a->W - 64);
        static const char *hint[] = { "‹ ›  una pagina", "su e giù  dieci pagine", "tasti laterali  cento pagine",
                                      "centro per salvare, indietro per annullare" };
        for (int i = 0; i < 4; i++) {
            int hw = font_text_width(FACE_MONO, 14, hint[i]);
            font_draw_text(&a->cv, FACE_MONO, 14, (a->W - hw) / 2, ui_baseline(FACE_MONO, 14, 406 + i * 26, 18),
                           hint[i], INK_555, 0);
        }
        full(a, first ? REFRESH_FULL : REFRESH_PARTIAL);
        first = 0;
        int raw;
        Key k = wait_key(a, &raw);
        int d = k == K_RIGHT ? 1 : k == K_LEFT ? -1 : k == K_UP ? 10 : k == K_DOWN ? -10 : k == K_NEXT ? 100
                : k == K_PREV ? -100 : 0;
        if (d) {
            p += d;
            p = p < 0 ? 0 : p > 9999 ? 9999 : p;
        } else if (k == K_SELECT) {
            if (print_pages_set(a->data, e, p) != 0) {
                message(a, "Non riesco a salvare le pagine", strerror(errno), REFRESH_PARTIAL);
                input_wait(3000, NULL);
                return 0;
            }
            return 1;
        } else if (k == K_BACK || k == K_MENU || k == K_HOME || k == K_TIMEOUT || k == K_QUIT)
            return 0;
    }
}

/* ------------------------------------------------------------ bookmarks */

/* dati/segnalibri.tsv: path, spine, byte, page, a few words of the page. */
typedef struct {
    int spine;
    uint32_t pos;
    int page;
    char words[96];
} Bookmark;

static int bookmarks_load(App *a, Bookmark *out, int max)
{
    char path[700];
    snprintf(path, sizeof path, "%s/segnalibri.tsv", a->data);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[2048];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *t = strchr(line, '\t');
        if (!t)
            continue;
        *t = 0;
        if (strcmp(line, a->entry->path) != 0)
            continue;
        Bookmark b = { 0 };
        int off = 0;
        if (sscanf(t + 1, "%d\t%u\t%d\t%n", &b.spine, &b.pos, &b.page, &off) < 3)
            continue;
        snprintf(b.words, sizeof b.words, "%s", off ? t + 1 + off : "");
        b.words[strcspn(b.words, "\r\n")] = 0;
        if (out && n < max)
            out[n] = b;
        n++;
    }
    fclose(f);
    return n;
}

/* Bookmark on the page on screen? (its byte falls inside the page) */
static int page_has_bookmark(App *a)
{
    if (!a->entry || !a->ch || a->pg.n == 0)
        return 0;
    Bookmark b[64];
    int n = bookmarks_load(a, b, 64);
    uint32_t from = a->pg.starts[a->page].pos;
    uint32_t to = a->page + 1 < a->pg.n ? a->pg.starts[a->page + 1].pos : UINT32_MAX;
    for (int i = 0; i < n && i < 64; i++)
        if (b[i].spine == a->spine && b[i].pos >= from && b[i].pos < to)
            return 1;
    return 0;
}

/* "02 · Lettura Segnalibro" (v0.6): the folded corner at the top right, 32 x 32, two greys split by the
 * diagonal: #AAA above, #CCC (the fold) below. */
static void draw_bookmark_corner(App *a)
{
    canvas_fill(&a->cv, a->W - 32, 0, 32, 32, G_FUMO);
    float tri[6] = { (float)a->W - 32, 0, (float)a->W, 32, (float)a->W - 32, 32 };
    canvas_polygon(&a->cv, tri, 3, G_LEGGERO);
}

/* A few words from byte pos of the chapter, on one line. */
static void words_at(const Chapter *ch, uint32_t pos, char *out, size_t n)
{
    size_t j = 0;
    uint32_t i = pos;
    while (i < ch->len && j + 5 < n) {
        out[j++] = ch->text[i++];
    }
    while (j > 0 && ((unsigned char)out[j - 1] & 0xC0) == 0x80) /* do not cut a letter */
        j--;
    if (j > 0 && ((unsigned char)out[j - 1] & 0x80))
        j--;
    out[j] = 0;
    for (size_t k = 0; k < j; k++)
        if (out[k] == '\t' || out[k] == '\n' || out[k] == '\r')
            out[k] = ' ';
}

/* "Aggiungi segnalibro": adds a bookmark for the page on screen (or removes it if there is one). */
static void bookmark_toggle(App *a)
{
    char path[700], tmp[720];
    snprintf(path, sizeof path, "%s/segnalibri.tsv", a->data);
    snprintf(tmp, sizeof tmp, "%s.nuovo", path);
    uint32_t from = a->pg.starts[a->page].pos;
    uint32_t to = a->page + 1 < a->pg.n ? a->pg.starts[a->page + 1].pos : UINT32_MAX;
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!out) {
        if (in)
            fclose(in);
        return;
    }
    int removed = 0;
    char line[2048];
    size_t pl = strlen(a->entry->path);
    while (in && fgets(line, sizeof line, in)) {
        int sp;
        unsigned pos;
        if (strncmp(line, a->entry->path, pl) == 0 && line[pl] == '\t' &&
            sscanf(line + pl + 1, "%d\t%u", &sp, &pos) == 2 && sp == a->spine && pos >= from && pos < to) {
            removed = 1;
            continue;
        }
        fputs(line, out);
    }
    if (in)
        fclose(in);
    if (!removed) {
        char words[96];
        words_at(a->ch, from, words, sizeof words);
        int p, t;
        book_pages(a, &p, &t);
        fprintf(out, "%s\t%d\t%u\t%d\t%s\n", a->entry->path, a->spine, from, p, words);
    }
    fclose(out);
    rename(tmp, path);
    log_msg("bookmark %s: spine %d byte %u", removed ? "removed" : "added", a->spine, from);
}

/* "Segnalibri": the list, choosing one goes there. Returns 1 if the page changed. */
static int bookmarks_run(App *a)
{
    Bookmark b[200];
    int n = bookmarks_load(a, b, 200);
    if (n > 200)
        n = 200;
    if (n == 0) {
        message(a, "Nessun segnalibro", "Aggiungilo dal menu, sulla pagina che vuoi ritrovare.", REFRESH_PARTIAL);
        input_wait(3000, NULL);
        return 0;
    }
    ListItem *items = xcalloc(n, sizeof(ListItem));
    char (*vals)[24] = xcalloc(n, 24);
    for (int i = 0; i < n; i++) {
        snprintf(vals[i], 24, "pag. %d", b[i].page);
        items[i].label = b[i].words[0] ? b[i].words : "Segnalibro";
        items[i].value = vals[i];
    }
    int r = list_run(a, a->book->title, "Segnalibri", items, n, 0, NULL);
    int moved = 0;
    if (r >= 0 && b[r].spine < a->book->nspine && load_chapter(a, b[r].spine, 1, b[r].pos) == 0) {
        save_position(a);
        moved = 1;
    }
    free(vals);
    free(items);
    return moved;
}

/* --------------------------------------------------------------- search */

typedef struct {
    int spine;
    uint32_t pos;
    int page;
    char words[96];
} Hit;

/* Case-insensitive for ASCII letters; accented letters must match exactly. */
static const char *find_ci(const char *hay, size_t hn, const char *needle, size_t nn, size_t from)
{
    for (size_t i = from; i + nn <= hn; i++) {
        size_t j = 0;
        while (j < nn) {
            unsigned char x = (unsigned char)hay[i + j], y = (unsigned char)needle[j];
            if (x >= 'A' && x <= 'Z')
                x += 32;
            if (y >= 'A' && y <= 'Z')
                y += 32;
            if (x != y)
                break;
            j++;
        }
        if (j == nn)
            return hay + i;
    }
    return NULL;
}

/* "Cerca nel libro": the word from the keyboard, then the list of the places found. */
static int search_run(App *a)
{
    static char query[64];
    if (!keyboard_run(a, a->book->title, "Cerca nel libro", query, sizeof query) || !query[0])
        return 0;
    message(a, "Cerco nel libro…", query, REFRESH_PARTIAL);
    int max = 100, n = 0;
    Hit *hits = xcalloc(max, sizeof(Hit));
    size_t qn = strlen(query);
    double t0 = now_ms();
    for (int s = 0; s < a->book->nspine && n < max; s++) {
        Chapter *c = s == a->spine ? a->ch : book_chapter(a->book, s);
        if (!c)
            continue;
        size_t from = 0;
        const char *h;
        while (n < max && (h = find_ci(c->text, c->len, query, qn, from))) {
            uint32_t pos = (uint32_t)(h - c->text);
            uint32_t st = pos > 30 ? pos - 30 : 0;
            while (st > 0 && ((unsigned char)c->text[st] & 0xC0) == 0x80)
                st--;
            while (st > 0 && st < pos && c->text[st - 1] != ' ') /* start at a word */
                st++;
            hits[n].spine = s;
            hits[n].pos = pos;
            hits[n].page = page_of_pos(a, s, pos, c->len);
            words_at(c, st, hits[n].words, sizeof hits[n].words);
            n++;
            from = pos + qn;
        }
        if (c != a->ch)
            chapter_free(c);
    }
    log_msg("search \"%s\": %d results in %.0f ms", query, n, now_ms() - t0);
    int moved = 0;
    if (n == 0) {
        message(a, "Nessun risultato", query, REFRESH_PARTIAL);
        input_wait(3000, NULL);
    } else {
        ListItem *items = xcalloc(n, sizeof(ListItem));
        char (*vals)[24] = xcalloc(n, 24);
        for (int i = 0; i < n; i++) {
            snprintf(vals[i], 24, "pag. %d", hits[i].page);
            items[i].label = hits[i].words;
            items[i].value = vals[i];
        }
        char title[96];
        snprintf(title, sizeof title, "«%s»%s", query, n == max ? " (primi 100)" : "");
        int r = list_run(a, a->book->title, title, items, n, 0, NULL);
        if (r >= 0 && load_chapter(a, hits[r].spine, 1, hits[r].pos) == 0) {
            save_position(a);
            moved = 1;
        }
        free(vals);
        free(items);
    }
    free(hits);
    return moved;
}

/* ---------------------------------------------------------- highlights */

/* dati/evidenziazioni.tsv: path, spine, first byte, end byte, page, the text (Simone, 18 nov). */
typedef struct Highlight {
    int spine;
    uint32_t start, end;
    int page;
    char text[400];
} Highlight;

#define MAX_HLS 500

static void highlights_load(App *a)
{
    free(a->hls);
    a->hls = NULL;
    a->nhls = 0;
    char path[700];
    snprintf(path, sizeof path, "%s/evidenziazioni.tsv", a->data);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[4096];
    size_t pl = strlen(a->entry->path);
    while (fgets(line, sizeof line, f) && a->nhls < MAX_HLS) {
        if (strncmp(line, a->entry->path, pl) != 0 || line[pl] != '\t')
            continue;
        Highlight h = { 0 };
        int off = 0;
        if (sscanf(line + pl + 1, "%d\t%u\t%u\t%d\t%n", &h.spine, &h.start, &h.end, &h.page, &off) < 4 || !off)
            continue;
        snprintf(h.text, sizeof h.text, "%s", line + pl + 1 + off);
        h.text[strcspn(h.text, "\r\n")] = 0;
        a->hls = xrealloc(a->hls, (a->nhls + 1) * sizeof(Highlight));
        a->hls[a->nhls++] = h;
    }
    fclose(f);
}

/* The saved highlights of the open chapter, as underlines. */
static int page_marks(App *a, TextMark *out, int max)
{
    int n = 0;
    for (int i = 0; i < a->nhls && n < max; i++)
        if (a->hls[i].spine == a->spine)
            out[n++] = (TextMark){ a->hls[i].start, a->hls[i].end, MARK_SAVED };
    return n;
}

static void highlight_save(App *a, uint32_t start, uint32_t end)
{
    char path[700];
    snprintf(path, sizeof path, "%s/evidenziazioni.tsv", a->data);
    FILE *f = fopen(path, "a");
    if (!f) {
        log_msg("highlight: cannot write %s: %s", path, strerror(errno));
        return;
    }
    char text[400];
    size_t j = 0;
    for (uint32_t i = start; i < end && i < a->ch->len && j + 1 < sizeof text; i++)
        text[j++] = a->ch->text[i] == '\t' ? ' ' : a->ch->text[i];
    while (j > 0 && j + 1 >= sizeof text - 1 && ((unsigned char)text[j - 1] & 0xC0) == 0x80)
        j--; /* do not cut a letter */
    if (j > 0 && j + 1 >= sizeof text - 1 && ((unsigned char)text[j - 1] & 0x80))
        j--;
    text[j] = 0;
    int p, t;
    book_pages(a, &p, &t);
    fprintf(f, "%s\t%d\t%u\t%u\t%d\t%s\n", a->entry->path, a->spine, start, end, p, text);
    fclose(f);
    log_msg("highlight saved: spine %d bytes %u-%u", a->spine, start, end);
    highlights_load(a);
}

/* Draws the page and collects its words. */
static void page_words(App *a, WordList *wl)
{
    wl->n = 0;
    layout_words = wl;
    draw_page(a);
    layout_words = NULL;
}

/* The word on the line above (dir -1) or below (+1) nearest to x. */
static int word_on_line(const WordList *wl, int cur, int dir)
{
    int top = wl->w[cur].top, x = (wl->w[cur].x0 + wl->w[cur].x1) / 2;
    int line = -1;
    for (int i = cur + dir; i >= 0 && i < wl->n; i += dir)
        if (wl->w[i].top != top) {
            line = wl->w[i].top;
            break;
        }
    if (line < 0)
        return cur;
    int best = cur, bd = 1 << 30;
    for (int i = 0; i < wl->n; i++)
        if (wl->w[i].top == line) {
            int d = abs((wl->w[i].x0 + wl->w[i].x1) / 2 - x);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    return best;
}

/* The cursor sits 7 px before a line, or in the middle of the space before a word. */
static int caret_x(const WordList *wl, int cur)
{
    const WordBox *w = &wl->w[cur];
    if (cur > 0 && wl->w[cur - 1].top == w->top)
        return (wl->w[cur - 1].x1 + w->x0) / 2 - 2;
    return w->x0 - 7;
}

/* "Inizia a evidenziare" (Simone, 18 nov; Figma "02 · Lettura · Evidenzia 1-3"): a cursor blinks at the
 * start of the line in the middle of the page; the 5-way pad moves it by word (left/right) or line (up/
 * down), the page keys turn the page within the chapter. Centre starts the selection (white on #222),
 * the pad extends it, centre again saves it (it stays underlined), Indietro cancels. */
#define CARET_BLINK_MS 600
#define CARET_BLINKS 100 /* about a minute, then it stays on */
static void highlight_run(App *a)
{
    WordList wl = { 0 };
    int cur = -1, selecting = 0;
    uint32_t anchor_s = 0, anchor_e = 0;
    int blinks = 0, on = 1, first = 1;
    a->sel.kind = 0;
    for (;;) {
        a->caret_h = 0;
        a->sel.kind = 0;
        a->foot = selecting ? "centro per salvare, indietro per annullare" : "centro per iniziare a evidenziare";
        page_words(a, &wl);
        if (wl.n == 0)
            break;
        if (cur < 0 || cur >= wl.n) { /* the first word of the line nearest to the middle of the page */
            int best = 0, bd = 1 << 30;
            for (int i = 0; i < wl.n; i++) {
                int d = abs(wl.w[i].top + wl.w[i].lh / 2 - a->H / 2);
                if (d < bd) {
                    bd = d;
                    best = i;
                }
            }
            while (best > 0 && wl.w[best - 1].top == wl.w[best].top)
                best--;
            cur = best;
        }
        const WordBox *w = &wl.w[cur];
        if (selecting) {
            uint32_t s0 = anchor_s < w->start ? anchor_s : w->start;
            uint32_t e0 = anchor_e > w->end ? anchor_e : w->end;
            a->sel = (TextMark){ s0, e0, MARK_SELECTION };
        } else if (on) {
            a->caret_x = caret_x(&wl, cur); /* Figma: x 41 for a line starting at 48 */
            a->caret_top = w->top;
            a->caret_h = w->lh;
        }
        draw_page(a);
        full(a, first ? REFRESH_FULL : REFRESH_PARTIAL);
        first = 0;
        int raw;
        Key k;
        if (!selecting && blinks < CARET_BLINKS) {
            k = input_wait(CARET_BLINK_MS, &raw);
            if (k == K_TIMEOUT) { /* blink: only the cursor changes */
                on = !on;
                blinks++;
                a->caret_x = caret_x(&wl, cur);
                a->caret_top = w->top;
                a->caret_h = on ? w->lh : 0;
                draw_page(a);
                display_update(&a->cv, (Rect){ a->caret_x, w->top, 4, w->lh }, REFRESH_PARTIAL);
                for (;;) {
                    k = input_wait(CARET_BLINK_MS, &raw);
                    if (k != K_TIMEOUT || ++blinks >= CARET_BLINKS)
                        break;
                    on = !on;
                    a->caret_h = on ? w->lh : 0;
                    draw_page(a);
                    display_update(&a->cv, (Rect){ a->caret_x, w->top, 4, w->lh }, REFRESH_PARTIAL);
                }
                if (k == K_TIMEOUT) { /* stop blinking: the cursor stays */
                    on = 1;
                    continue;
                }
            }
            if (k == K_SLEEP) {
                standby(a);
                continue;
            }
            if (k == K_WAKE)
                continue;
            touch_screensaver();
        } else
            k = wait_key(a, &raw);
        on = 1;
        if (k != K_NONE)
            blinks = 0;
        if (k == K_RIGHT)
            cur = cur + 1 < wl.n ? cur + 1 : cur;
        else if (k == K_LEFT)
            cur = cur > 0 ? cur - 1 : cur;
        else if (k == K_DOWN || k == K_UP)
            cur = word_on_line(&wl, cur, k == K_DOWN ? 1 : -1);
        else if (k == K_NEXT || k == K_PREV) { /* other page of the same chapter */
            int np = a->page + (k == K_NEXT ? 1 : -1);
            if (np >= 0 && np < a->pg.n) {
                a->page = np;
                cur = selecting ? (k == K_NEXT ? 0 : 1 << 20) : -1;
                if (selecting && k == K_PREV) { /* the last word of the previous page */
                    page_words(a, &wl);
                    cur = wl.n - 1;
                }
                first = 1;
            }
        } else if (k == K_SELECT) {
            if (!selecting) {
                selecting = 1;
                anchor_s = w->start;
                anchor_e = w->end;
            } else {
                highlight_save(a, a->sel.start, a->sel.end);
                break;
            }
        } else if (k == K_BACK || k == K_MENU || k == K_HOME || k == K_TIMEOUT || k == K_QUIT)
            break;
    }
    a->sel.kind = 0;
    a->caret_h = 0;
    a->foot = NULL;
    free(wl.w);
    save_position(a);
}

/* "Evidenziazioni": the list of the saved highlights, choosing one goes there. */
static int highlights_run(App *a)
{
    if (a->nhls == 0) {
        message(a, "Nessuna evidenziazione", "Scegli «Inizia a evidenziare» dal menu.", REFRESH_PARTIAL);
        input_wait(3000, NULL);
        return 0;
    }
    int n = a->nhls;
    ListItem *items = xcalloc(n, sizeof(ListItem));
    char (*vals)[24] = xcalloc(n, 24);
    for (int i = 0; i < n; i++) {
        snprintf(vals[i], 24, "pag. %d", a->hls[i].page);
        items[i].label = a->hls[i].text;
        items[i].value = vals[i];
    }
    int r = list_run(a, a->book->title, "Evidenziazioni", items, n, 0, NULL);
    int moved = 0;
    if (r >= 0 && a->hls[r].spine < a->book->nspine && load_chapter(a, a->hls[r].spine, 1, a->hls[r].start) == 0) {
        save_position(a);
        moved = 1;
    }
    free(vals);
    free(items);
    return moved;
}

/* Right/left of the pad while reading (Simone, 18 nov): next/previous chapter; left goes first to the
 * start of the chapter on screen. A book in one piece moves 10 pages at a time. */
static void chapter_jump(App *a, int dir)
{
    Book *b = a->book;
    if (b->nspine <= 1) {
        int p, t;
        book_pages(a, &p, &t);
        p += dir * 10;
        goto_page(a, p < 1 ? 1 : p > t ? t : p);
        return;
    }
    int target = -1;
    int have_toc = 0; /* a table of contents that names several spine items */
    for (int i = 1; i < b->ntoc; i++)
        if (b->toc[i].spine != b->toc[0].spine)
            have_toc = 1;
    if (have_toc) {
        int here = -1; /* start of the chapter on screen */
        for (int i = 0; i < b->ntoc; i++)
            if (b->toc[i].spine <= a->spine && b->toc[i].spine > here)
                here = b->toc[i].spine;
        if (dir > 0) {
            for (int i = 0; i < b->ntoc; i++)
                if (b->toc[i].spine > a->spine && (target < 0 || b->toc[i].spine < target))
                    target = b->toc[i].spine;
        } else if (here >= 0 && (here < a->spine || a->page > 0))
            target = here;
        else
            for (int i = 0; i < b->ntoc; i++)
                if (b->toc[i].spine < a->spine && b->toc[i].spine > target)
                    target = b->toc[i].spine;
    } else if (dir > 0)
        target = a->spine + 1 < b->nspine ? a->spine + 1 : -1;
    else
        target = a->page > 0 ? a->spine : a->spine - 1;
    if (target < 0 || target >= b->nspine)
        return;
    if (target == a->spine)
        a->page = 0;
    else if (load_chapter(a, target, 0, 0) != 0)
        return;
    log_msg("chapter %s: spine %d", dir > 0 ? "next" : "previous", a->spine);
    save_position(a);
}

/* "02 · Lettura Menù" (Pannello menu, Property 1=Libro): the panel opens over the page, which stays
 * visible above it; texts, values and icons exactly as in Figma. Returns where to go after the menu,
 * or -1 to keep reading. */
static int reader_menu(App *a)
{
    a->goal_note_until = 0; /* the note is not redrawn over the menu */
    int sel = 0;
    enum { M_MARK, M_HIGHLIGHT, M_SEARCH, M_SIZE, M_MARKS, M_HLS, M_PRINT, M_N };
    Row rows[M_N] = { /* Simone, 18 nov: "Vai a pagina…" and "Indice" out, the highlights in */
        { "Aggiungi segnalibro", "", ICON_PLUS },
        { "Inizia a evidenziare", "", ICON_PLUS },
        { "Cerca nel libro", "", ICON_ARROW },
        { "Dimensione carattere", "", ICON_NONE },
        { "Segnalibri", "", ICON_ARROW },
        { "Evidenziazioni", "", ICON_ARROW },
        { "Pagine del cartaceo", "", ICON_ARROW }, /* Simone, 17 nov: written by hand */
    };
    int redraw = 1;
    for (;;) {
        if (redraw) { /* the page under the panel, with the title in the header */
            draw_page(a);
            ui_header(&a->cv, a->book->title, NULL, -1);
            full(a, REFRESH_PARTIAL);
            redraw = 0;
        }
        int p, t;
        book_pages(a, &p, &t);
        snprintf(rows[M_SIZE].value, sizeof rows[M_SIZE].value, "%d pt", a->lp.size);
        if (a->nhls)
            snprintf(rows[M_HLS].value, sizeof rows[M_HLS].value, "%d", a->nhls);
        else
            rows[M_HLS].value[0] = 0;
        if (a->entry->print_pages > 0)
            snprintf(rows[M_PRINT].value, sizeof rows[M_PRINT].value, "%d", a->entry->print_pages);
        else
            rows[M_PRINT].value[0] = 0;
        int nb = bookmarks_load(a, NULL, 0);
        if (nb)
            snprintf(rows[M_MARKS].value, sizeof rows[M_MARKS].value, "%d", nb);
        else
            rows[M_MARKS].value[0] = 0;
        char left[16], right[48];
        snprintf(left, sizeof left, "%d%%", percent_of(a));
        snprintf(right, sizeof right, "pagina %d di %d", p, t);
        Key k = K_NONE;
        int r = panel_run(a, rows, M_N, &sel, left, NULL, right, percent_of(a) / 100.0, 1u << M_SIZE, &k);
        switch (r) {
        case -2: /* left/right on the size row */
            change_size(a, k == K_RIGHT ? 1 : -1);
            sel = M_SIZE;
            redraw = 1;
            continue;
        case M_SIZE: /* centre: next size, after the biggest back to the smallest */
            change_size(a, size_index(a->lp.size) == NSIZES - 1 ? -(NSIZES - 1) : 1);
            sel = M_SIZE;
            redraw = 1;
            continue;
        case M_MARK: bookmark_toggle(a); return -1;
        case M_SEARCH: search_run(a); return -1;
        case M_HIGHLIGHT: highlight_run(a); return -1;
        case M_MARKS: bookmarks_run(a); return -1;
        case M_HLS: highlights_run(a); return -1;
        case M_PRINT: print_pages_run(a); return -1;
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
    highlights_load(a);
    a->session_start = a->last_turn = time(NULL);
    a->session_pages = 0;
    if (a->goal_min > 0 && minutes_today(a) >= a->goal_min) /* already reached today: no announcement */
        a->goal_note_day = day_number(time(NULL));
    a->turns = 0;
    render_page(a, REFRESH_FULL);
    Next next = GO_LIBRARY;
    for (;;) {
        int raw;
        Key k = wait_key(a, &raw);
        if (k == K_NEXT || k == K_DOWN)
            turn(a, +1);
        else if (k == K_PREV || k == K_UP)
            turn(a, -1);
        else if (k == K_RIGHT || k == K_LEFT) {
            chapter_jump(a, k == K_RIGHT ? 1 : -1);
            a->turns = 0;
            render_page(a, REFRESH_FULL);
        }
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
    reading_log(a->data, a->entry->path, a->session_start, session_end(a, time(NULL)), a->session_pages);
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

/* Grid geometry from "00 · Home 3 colonne (scorrimento)", 3:4 covers since v1.0 */
#define G_X0 19.0f
#define G_Y0 72.0f
#define G_CW 180.667f
#define G_CH 261.77f /* v1.0: 3:4 covers */
#define G_GX 10.0f
#define G_GY 14.0f
#define G_FRAME_H 238.02f

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
static int collections(App *a, char names[][128], int max);

#define ARCHIVE_NAME "Archivio" /* "Archivia libro" moves books here; shown only from the settings (v1.0.4) */

/* The Kindle's own file of highlights and notes ("My Clippings.txt", "I miei ritagli" in Italian). */
static int is_clippings(const LibEntry *e)
{
    const char *b = strrchr(e->path, '/');
    b = b ? b + 1 : e->path;
    return !strcasecmp(b, "My Clippings.txt") || !strcasecmp(b, "I miei ritagli.txt") ||
           (e->title && !strcasecmp(e->title, "I miei ritagli"));
}

static LibEntry *clippings_entry(App *a)
{
    for (int i = 0; i < a->lib.n; i++)
        if (is_clippings(&a->lib.e[i]))
            return &a->lib.e[i];
    return NULL;
}

static int build_cells(App *a, const char *folder, int read_view, Cell **out)
{
    Cell *cells = xcalloc(a->lib.n + 2 + 64, sizeof(Cell)); /* + up to 64 empty collections */
    int n = 0;
    Cell read = { .kind = CELL_READ };
    snprintf(read.name, sizeof read.name, "Libri letti");
    for (int i = 0; i < a->lib.n; i++) {
        LibEntry *e = &a->lib.e[i];
        int done = e->percent >= 100;
        char f[128];
        folder_of(a, e, f, sizeof f);
        if (is_clippings(e))
            continue; /* "I miei ritagli": only from Impostazioni > Mostra sottolineature (v1.0.4) */
        if (!(folder && strcmp(folder, ARCHIVE_NAME) == 0) && strcmp(f, ARCHIVE_NAME) == 0)
            continue; /* archived: out of the Home and of the books read */
        if (folder && strcmp(folder, ARCHIVE_NAME) == 0) { /* "Mostra archivio": read ones too */
            if (strcmp(f, ARCHIVE_NAME) == 0)
                cells[n++] = (Cell){ .kind = CELL_BOOK, .e = e, .recent = recent_of(e) };
            continue;
        }
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
    if (!folder && !read_view) { /* empty collections made with "Crea nuova raccolta" */
        char (*names)[128] = xcalloc(64, 128);
        int nc = collections(a, names, 64);
        for (int c = 0; c < nc; c++) {
            int j;
            for (j = 0; j < n && !(cells[j].kind == CELL_FOLDER && strcmp(cells[j].name, names[c]) == 0); j++)
                ;
            if (j == n) {
                cells[n] = (Cell){ .kind = CELL_FOLDER };
                snprintf(cells[n].name, sizeof cells[n].name, "%s", names[c]);
                n++;
            }
        }
        free(names);
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
    if (badge) { /* "Nuovo": black box 45 x 17, white text 4 px in, as in Figma */
        int tw = font_text_width(FACE_MONO_MEDIUM, 13, left);
        canvas_fill(&a->cv, lx, (int)lrintf(top - 1), tw + 8, 17, G_NERO);
        font_draw_text(&a->cv, FACE_MONO_MEDIUM, 13, lx + 4, base + 1, left, G_CARTA, 0);
    } else if (left)
        font_draw_text(&a->cv, FACE_MONO_MEDIUM, 13, lx, base, left, G_NERO, right ? 100 : (int)G_CW - 10);
    if (right) {
        int tw = font_text_width(FACE_MONO_MEDIUM, 13, right);
        font_draw_text(&a->cv, FACE_MONO_MEDIUM, 13, (int)lrintf(x + G_CW - 5 - tw), base, right, G_NERO, 0);
    }
}

/* Pages shown on the Home: the printed edition when known (Simone, 17 nov), else the screen pages. */
static int pages_of(App *a, const LibEntry *e)
{
    if (e->print_pages > 0)
        return e->print_pages;
    if (e->pages > 0)
        return e->pages;
    if (e->text_bytes <= 0)
        return 0;
    int p = (int)(e->text_bytes / default_bpp(a->lp.size) + 0.5);
    return p < 1 ? 1 : p;
}

static void frame_clip(Canvas *cv, float x, float y)
{
    canvas_clip(cv, (int)lrintf(x), (int)lrintf(y), (int)lrintf(G_CW), (int)lrintf(G_FRAME_H));
}

/* "Raccolte" as two office binders side by side: Figma component "Raccolta 600x800 / Due faldoni"
 * (99:1927, as adjusted by Simone on 2026-11-17), scaled to the 172 x 229 cover box. Blank labels: the
 * name and the count stay in the info row under the cell. */
static void folder_binders(App *a, float x, float y)
{
    Canvas *cv = &a->cv;
    const float sx = 172.064f / 600.0f, sy = 229.41f / 800.0f;
    x += 4.3f;
    y += 4.3f;
#define BX(v) ((int)lrintf(x + (v) * sx))
#define BY(v) ((int)lrintf(y + (v) * sy))
#define BOX(l, t, w, h, ink) canvas_fill(cv, BX(l), BY(t), BX((l) + (w)) - BX(l), BY((t) + (h)) - BY(t), ink)
    static const uint8_t spine[2] = { 255 - 0x88, 255 - 0x55 };
    for (int i = 0; i < 2; i++) {
        float o = i * 307.0f;
        BOX(o, 0, 293, 800, 255 - 0x22);                 /* case */
        BOX(o + 24, 21, 245, 758, spine[i]);             /* spine */
        canvas_frame(cv, BX(o + 112), BY(63), BX(o + 182) - BX(o + 112), BY(133) - BY(63), 2, G_CARTA); /* mark */
        BOX(o + 49, 212, 195, 335, G_CARTA);             /* label */
        BOX(o + 63, 324, 167, 7, 255 - 0x55);            /* rules */
        BOX(o + 63, 435, 167, 7, 255 - 0x55);
        canvas_disc(cv, x + (o + 146.5f) * sx, y + 670.5f * sy, 45.5f * sx, 255 - 0xDD); /* ring */
        canvas_disc(cv, x + (o + 146.5f) * sx, y + 670.5f * sy, 31.5f * sx, G_NERO);     /* hole */
    }
#undef BOX
#undef BX
#undef BY
}

static void draw_cell(App *a, const Cell *c, float x, float y, int focus)
{
    Canvas *cv = &a->cv;
    Rect r = { (int)x - 1, (int)y - 1, (int)G_CW + 3, (int)G_CH + 3 };
    canvas_fill(cv, r.x, r.y, r.w, r.h, G_CARTA);
    /* "Focus = cornice nera di 3 px" (v0.6): a square frame around the cell, the cover does not move */
    frame_clip(cv, x, y);
    if (c->kind == CELL_BOOK) {
        cover_draw(cv, x + 4.3f, y + 4.3f, 172.064f, 229.41f, c->e->title, c->e->author, c->e->publisher, 0);
    } else {
        folder_binders(a, x, y);
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
        else { /* never opened: the black "NUOVO" label (v0.6) */
            badge = 1;
            snprintf(left, sizeof left, "NUOVO");
        }
        int p = pages_of(a, e); /* only the number, as in Figma since v0.6 */
        snprintf(right, sizeof right, "%d", p);
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

/* ------------------------------------------------- collections, to-read */

#define COLL_MARK ".carbonella-raccolta" /* marks a folder made with "Crea nuova raccolta" */

/* Names of the collections: sub-folders of the books folder that hold books, plus the empty ones
 * created from the menu. Returns how many (at most max). */
static int collections(App *a, char names[][128], int max)
{
    int n = 0;
    for (int i = 0; i < a->lib.n; i++) {
        char f[128];
        folder_of(a, &a->lib.e[i], f, sizeof f);
        if (!*f || strcmp(f, ARCHIVE_NAME) == 0)
            continue;
        int j;
        for (j = 0; j < n && strcmp(names[j], f) != 0; j++)
            ;
        if (j == n && n < max)
            snprintf(names[n++], 128, "%s", f);
    }
    DIR *d = opendir(a->books);
    struct dirent *de;
    while (d && (de = readdir(d)) && n < max) {
        if (de->d_name[0] == '.')
            continue;
        char mark[1024];
        snprintf(mark, sizeof mark, "%s/%s/" COLL_MARK, a->books, de->d_name);
        if (access(mark, F_OK) != 0 || strcmp(de->d_name, ARCHIVE_NAME) == 0)
            continue;
        int j;
        for (j = 0; j < n && strcmp(names[j], de->d_name) != 0; j++)
            ;
        if (j == n)
            snprintf(names[n++], 128, "%.127s", de->d_name);
    }
    if (d)
        closedir(d);
    return n;
}

/* Trims spaces and refuses names that cannot be a folder. */
static int clean_name(char *s)
{
    size_t l = strlen(s);
    while (l && s[l - 1] == ' ')
        s[--l] = 0;
    size_t st = strspn(s, " ");
    memmove(s, s + st, l - st + 1);
    return s[0] && s[0] != '.' && !strchr(s, '/');
}

/* "Crea nuova raccolta": the name from the keyboard, then a new folder. Returns 1 if created;
 * the name goes in out (if not NULL). */
static int create_collection(App *a, char *out, size_t n)
{
    char name[64] = "";
    if (!keyboard_run(a, a->name, "Crea nuova raccolta", name, sizeof name))
        return 0;
    if (!clean_name(name)) {
        message(a, "Nome non valido", "Scrivi almeno una lettera.", REFRESH_PARTIAL);
        input_wait(3000, NULL);
        return 0;
    }
    char dir[1024], mark[1100];
    snprintf(dir, sizeof dir, "%s/%s", a->books, name);
    snprintf(mark, sizeof mark, "%s/" COLL_MARK, dir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        message(a, "Non riesco a creare la raccolta", strerror(errno), REFRESH_PARTIAL);
        input_wait(4000, NULL);
        return 0;
    }
    FILE *f = fopen(mark, "w");
    if (f)
        fclose(f);
    log_msg("collection created: %s", name);
    if (out)
        snprintf(out, n, "%s", name);
    message(a, "Raccolta creata", name, REFRESH_PARTIAL);
    input_wait(1500, NULL);
    return 1;
}

static int cmp_entry_title(const void *x, const void *y)
{
    const LibEntry *a = *(LibEntry *const *)x, *b = *(LibEntry *const *)y;
    return strcasecmp(a->title ? a->title : "", b->title ? b->title : "");
}

/* "Aggiungi alla raccolta": the focused book (or, on a collection, a book chosen from the list) goes
 * into a collection (or out of it). Returns 1 if a book moved. */
static int move_books(App *a, LibEntry *focus)
{
    if (a->lib.n == 0)
        return 0;
    LibEntry **books = xcalloc(a->lib.n, sizeof(LibEntry *));
    ListItem *items = xcalloc(a->lib.n, sizeof(ListItem));
    char (*where)[128] = xcalloc(a->lib.n, 128);
    for (int i = 0; i < a->lib.n; i++)
        books[i] = &a->lib.e[i];
    qsort(books, a->lib.n, sizeof(LibEntry *), cmp_entry_title);
    for (int i = 0; i < a->lib.n; i++) {
        folder_of(a, books[i], where[i], 128);
        items[i].label = books[i]->title;
        items[i].value = where[i];
    }
    int moved = 0;
    int r = -1;
    if (focus) {
        for (int i = 0; i < a->lib.n; i++)
            if (books[i] == focus)
                r = i;
    } else
        r = list_run(a, a->name, "Aggiungi alla raccolta", items, a->lib.n, 0, NULL);
    if (r >= 0) {
        LibEntry *e = books[r];
        /* as in Figma: "Crea nuova raccolta" (+), then the collections (›) */
        char (*names)[128] = xcalloc(64, 128);
        int nc = collections(a, names, POPUP_MAX - 1);
        Row *dest = xcalloc(nc + 1, sizeof(Row));
        dest[0] = (Row){ "Crea nuova raccolta", "", ICON_PLUS };
        for (int i = 0; i < nc; i++)
            dest[i + 1] = (Row){ names[i], "", ICON_ARROW };
        int d = popup_run(a, dest, nc + 1, nc > 0 ? 1 : 0);
        char target[128] = "";
        int ok = d >= 0;
        if (d == 0)
            ok = create_collection(a, target, sizeof target);
        else if (d > 0)
            snprintf(target, sizeof target, "%s", names[d - 1]);
        if (ok) {
            const char *base = strrchr(e->path, '/');
            base = base ? base + 1 : e->path;
            char np[1024];
            if (*target)
                snprintf(np, sizeof np, "%s/%s/%s", a->books, target, base);
            else
                snprintf(np, sizeof np, "%s/%s", a->books, base);
            if (strcmp(np, e->path) == 0)
                ;
            else if (access(np, F_OK) == 0) {
                message(a, "C'è già un file con questo nome", base, REFRESH_PARTIAL);
                input_wait(4000, NULL);
            } else if (rename(e->path, np) != 0) {
                message(a, "Non riesco a spostare il libro", strerror(errno), REFRESH_PARTIAL);
                input_wait(4000, NULL);
            } else {
                log_msg("moved %s -> %s", e->path, np);
                free(e->path);
                e->path = xstrdup(np);
                library_save_positions(&a->lib, a->data); /* the reading position follows the book */
                moved = 1;
            }
        }
        free(dest);
        free(names);
    }
    free(where);
    free(items);
    free(books);
    return moved;
}

/* "Archivia libro": the focused book goes into the "Archivio" collection (created if missing), so it
 * leaves the Home but stays on the Kindle. It comes back with "Aggiungi alla raccolta". */
static int archive_book(App *a, LibEntry *e)
{
    char dir[1024], mark[1100];
    snprintf(dir, sizeof dir, "%s/" ARCHIVE_NAME, a->books);
    snprintf(mark, sizeof mark, "%s/" COLL_MARK, dir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        message(a, "Non riesco ad archiviare il libro", strerror(errno), REFRESH_PARTIAL);
        input_wait(4000, NULL);
        return 0;
    }
    FILE *f = fopen(mark, "a");
    if (f)
        fclose(f);
    const char *base = strrchr(e->path, '/');
    base = base ? base + 1 : e->path;
    char np[1200];
    snprintf(np, sizeof np, "%s/%s", dir, base);
    if (strcmp(np, e->path) == 0)
        return 0;
    if (access(np, F_OK) == 0) {
        message(a, "C'è già un file con questo nome", base, REFRESH_PARTIAL);
        input_wait(4000, NULL);
        return 0;
    }
    if (rename(e->path, np) != 0) {
        message(a, "Non riesco ad archiviare il libro", strerror(errno), REFRESH_PARTIAL);
        input_wait(4000, NULL);
        return 0;
    }
    log_msg("archived %s -> %s", e->path, np);
    free(e->path);
    e->path = xstrdup(np);
    library_save_positions(&a->lib, a->data);
    message(a, "Libro archiviato", e->title, REFRESH_PARTIAL);
    input_wait(1500, NULL);
    return 1;
}

/* "Togli dall'archivio": back to the books folder, so the book is in the Home again. */
static int unarchive_book(App *a, LibEntry *e)
{
    const char *base = strrchr(e->path, '/');
    base = base ? base + 1 : e->path;
    char np[1200];
    snprintf(np, sizeof np, "%s/%s", a->books, base);
    if (access(np, F_OK) == 0) {
        message(a, "C'è già un file con questo nome", base, REFRESH_PARTIAL);
        input_wait(4000, NULL);
        return 0;
    }
    if (rename(e->path, np) != 0) {
        message(a, "Non riesco a togliere il libro dall'archivio", strerror(errno), REFRESH_PARTIAL);
        input_wait(4000, NULL);
        return 0;
    }
    log_msg("unarchived %s -> %s", e->path, np);
    free(e->path);
    e->path = xstrdup(np);
    library_save_positions(&a->lib, a->data);
    message(a, "Libro tolto dall'archivio", e->title, REFRESH_PARTIAL);
    input_wait(1500, NULL);
    return 1;
}

/* "Elimina archivio definitivamente": deletes the book files in the archive folder (only files, only
 * there). Returns how many. */
static int delete_archive(App *a)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/" ARCHIVE_NAME, a->books);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *de;
    int n = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char p[1300];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode) && unlink(p) == 0) {
            log_msg("deleted %s", p);
            n++;
        }
    }
    closedir(d);
    return n;
}

static void sort_step(App *a, int dir) { a->sort = (a->sort + (dir > 0 ? 1 : 2)) % 3; }

static void goal_step(App *a, int dir)
{
    int i = 0;
    while (i < NGOALS && GOALS[i] < a->goal_min)
        i++;
    i = (i + dir + NGOALS) % NGOALS;
    a->goal_min = GOALS[i];
}

/* "Salva come finito": the focused book goes among the books read. */
static void mark_finished(App *a, LibEntry *e)
{
    e->percent = 100;
    e->last_read = time(NULL);
    library_save_positions(&a->lib, a->data);
    log_msg("marked as finished: %s", e->path);
    message(a, "Salvato come finito", e->title, REFRESH_PARTIAL);
    input_wait(1500, NULL);
}

/* Home menu ("00 Home Menù", the instance in Simone's frame): texts, values and icons exactly as in
 * Figma. 'focus' is the book with the focus in the grid (NULL on a collection).
 * Returns 1 to read the library again, 2 to exit, 0 otherwise. */
static int home_menu(App *a, LibEntry *focus, const char *folder)
{
    int in_archive = folder && strcmp(folder, ARCHIVE_NAME) == 0;
    /* order from Figma "Pannello menu > Home" (Simone, 2026-11-17 v1.0.6) */
    enum { H_NEWCOLL, H_ADDCOLL, H_FINISHED, H_ARCHIVE, H_GOAL, H_SORT, H_RECEIVE, H_SETTINGS, H_N };
    Row rows[H_N] = {
        { "Crea nuova raccolta", "", ICON_PLUS },
        { "Aggiungi alla raccolta", "", ICON_ARROW },
        { "Salva come finito", "", ICON_ARROW },
        { "Archivia libro", "", ICON_ARROW },
        { "Obbiettivo giornaliero", "", ICON_NONE },
        { "Ordina per", "", ICON_NONE },
        { "Ricevi libri", "", ICON_ARROW }, /* Simone, 18 nov */
        { "Tutte le impostazioni", "", ICON_ARROW },
    };
    if (in_archive) /* Simone: in the archive "Archivia libro" becomes "Togli dall'archivio" */
        rows[H_ARCHIVE].label = "Togli dall’archivio";
    char ver[48];
    snprintf(ver, sizeof ver, "Carbonella %s", CARBONELLA_VERSION);
    int sel = 0, changed = 0;
    for (;;) {
        snprintf(rows[H_SORT].value, sizeof rows[H_SORT].value, "%s", SORTS[a->sort]);
        snprintf(rows[H_GOAL].value, sizeof rows[H_GOAL].value, "%d m", a->goal_min);
        Key k = K_NONE;
        int r = panel_run(a, rows, H_N, &sel, NULL, ver, NULL, 0, (1u << H_SORT) | (1u << H_GOAL), &k);
        int dir = k == K_LEFT ? -1 : 1;
        if (r == -2 || r == H_SORT || r == H_GOAL) { /* left/right, or centre = next value */
            if (sel == H_SORT)
                sort_step(a, dir);
            else
                goal_step(a, dir);
            save_prefs(a); /* the Home is drawn again when the menu closes */
            continue;
        }
        switch (r) {
        case H_NEWCOLL: return create_collection(a, NULL, 0) ? 1 : changed;
        case H_ADDCOLL: return move_books(a, focus) ? 1 : changed;
        case H_FINISHED:
            if (focus && focus->percent < 100) {
                mark_finished(a, focus);
                return 1;
            }
            message(a, "Scegli prima un libro", "Metti il riquadro su una copertina e riapri il menu.", REFRESH_PARTIAL);
            input_wait(3000, NULL);
            return changed;
        case H_ARCHIVE:
            if (focus && in_archive)
                return unarchive_book(a, focus) ? 1 : changed;
            if (focus) {
                if (!confirm_popup(a, "Sei sicuro di voler archiviare?"))
                    return changed;
                return archive_book(a, focus) ? 1 : changed;
            }
            message(a, "Scegli prima un libro", "Metti il riquadro su una copertina e riapri il menu.", REFRESH_PARTIAL);
            input_wait(3000, NULL);
            return changed;
        case H_RECEIVE: {
            int r = receive_run(a);
            return r == 2 ? 2 : r == 1 ? 1 : changed;
        }
        case H_SETTINGS: {
            int r = settings_run(a);
            return r == 2 ? 2 : r == 3 ? 3 : r == 4 ? 4 : r == 1 ? 1 : changed;
        }
        case PANEL_END: return k == K_HOME ? changed : 2;
        default: return changed;
        }
    }
}

/* "Cerca un libro" (Simone, 18 nov): the keyboard key on the Home opens the keyboard, then the books whose
 * title or author contain the words, in the list style of the settings. Returns the chosen book. */
static LibEntry *book_search(App *a)
{
    static char query[64];
    for (;;) {
        if (!keyboard_edit(a, a->name, "Cerca un libro", NULL, "Scrivi il titolo o l'autore.", 0, 0, query,
                           sizeof query) ||
            !query[0])
            return NULL;
        size_t qn = strlen(query);
        int n = 0;
        ListItem *items = xcalloc(a->lib.n ? a->lib.n : 1, sizeof *items);
        LibEntry **found = xcalloc(a->lib.n ? a->lib.n : 1, sizeof *found);
        for (int i = 0; i < a->lib.n; i++) {
            LibEntry *e = &a->lib.e[i];
            if (find_ci(e->title, strlen(e->title), query, qn, 0) || find_ci(e->author, strlen(e->author), query, qn, 0)) {
                items[n] = (ListItem){ e->title, e->author };
                found[n++] = e;
            }
        }
        log_msg("search: '%s' %d books", query, n);
        if (n == 0) {
            free(items);
            free(found);
            message(a, "Nessun libro trovato", query, REFRESH_PARTIAL);
            input_wait(2000, NULL);
            continue; /* back to the keyboard, the words stay */
        }
        char title[96];
        snprintf(title, sizeof title, n == 1 ? "1 libro" : "%d libri", n);
        Key k;
        int r = list_run(a, a->name, title, items, n, 0, &k);
        LibEntry *e = r >= 0 ? found[r] : NULL;
        free(items);
        free(found);
        if (e)
            return e;
        if (r == -2 && (k == K_HOME || k == K_TIMEOUT || k == K_QUIT))
            return NULL;
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
                int m = home_menu(a, NULL, NULL);
                if (m == 4 && (g_selected = clippings_entry(a))) {
                    result = GO_READER;
                    break;
                }
                if (m == 3) {
                    int r = grid_view(a, ARCHIVE_NAME, 0, NULL);
                    if (r != -1) {
                        result = r;
                        break;
                    }
                    continue;
                }
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
        case K_KEYBOARD: { /* "Cerca un libro" */
            LibEntry *e = book_search(a);
            if (e) {
                g_selected = e;
                result = GO_READER;
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
            int m = home_menu(a, cells[sel].kind == CELL_BOOK ? cells[sel].e : NULL, folder);
            if (m == 4 && (g_selected = clippings_entry(a))) { /* "Mostra sottolineature" */
                result = GO_READER;
                goto out;
            }
            if (m == 3) { /* "Mostra archivio" */
                int r = grid_view(a, ARCHIVE_NAME, 0, NULL);
                if (r != -1) {
                    result = r;
                    goto out;
                }
                result = GO_LIBRARY; /* books may have left the archive: read the library again */
                goto out;
            }
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

/* The five publisher models with the books of the Figma references, plus the same books with another
 * publisher, to compare with "Frame 6" (--copertine). */
static void cover_sheet(App *a)
{
    static const char *B[][3] = {
        { "La forma dell'acqua", "Andrea Camilleri", "Sellerio" },
        { "Le nozze di Cadmo e Armonia", "Roberto Calasso", "Adelphi" },
        { "Le nozze di Cadmo e Armonia", "Roberto Calasso", "Einaudi" },
        { "Cecità", "José Saramago", "Feltrinelli" },
        { "Cecità", "José Ernatdesaramago", "Bompiani" },
        { "Il barone rampante", "Italo Calvino", "Mondadori" },
        { "Il nome della rosa", "Umberto Eco", "La nave di Teseo" },
    };
    canvas_clear(&a->cv, G_CARTA);
    for (int i = 0; i < 7; i++) {
        float x = 24 + (i % 3) * 192, y = 16 + (i / 3) * 264;
        cover_draw(&a->cv, x, y, 168, 224, B[i][0], B[i][1], B[i][2], 0);
        font_draw_text(&a->cv, FACE_MONO, 14, (int)x, (int)y + 242, B[i][2], G_NERO, 168);
    }
    full(a, REFRESH_FULL);
}

/* v1.0 test board (--tavola FILE): 30 books without publisher, so model, grey step and illustration all
 * come from title and author, at the Home size (172 x 229) with model, motif and step under each. */
static int cover_board(const char *path)
{
    static const char *B[30][2] = {
        { "I promessi sposi", "Alessandro Manzoni" }, { "Il deserto dei Tartari", "Dino Buzzati" },
        { "Se una notte d'inverno un viaggiatore", "Italo Calvino" }, { "Se questo è un uomo", "Primo Levi" },
        { "La coscienza di Zeno", "Italo Svevo" }, { "Il gattopardo", "Giuseppe Tomasi di Lampedusa" },
        { "Il giardino dei Finzi-Contini", "Giorgio Bassani" }, { "La storia", "Elsa Morante" },
        { "Il partigiano Johnny", "Beppe Fenoglio" }, { "Il sentiero dei nidi di ragno", "Italo Calvino" },
        { "Uno, nessuno e centomila", "Luigi Pirandello" }, { "Il visconte dimezzato", "Italo Calvino" },
        { "La ragazza di Bube", "Carlo Cassola" }, { "Cristo si è fermato a Eboli", "Carlo Levi" },
        { "L'amica geniale", "Elena Ferrante" }, { "Seta", "Alessandro Baricco" },
        { "Va' dove ti porta il cuore", "Susanna Tamaro" }, { "Il giorno della civetta", "Leonardo Sciascia" },
        { "Marcovaldo", "Italo Calvino" }, { "Il fu Mattia Pascal", "Luigi Pirandello" },
        { "La noia", "Alberto Moravia" }, { "Il conformista", "Alberto Moravia" },
        { "Lessico famigliare", "Natalia Ginzburg" }, { "Le città invisibili", "Italo Calvino" },
        { "Il pendolo di Foucault", "Umberto Eco" }, { "Novecento", "Alessandro Baricco" },
        { "La luna e i falò", "Cesare Pavese" }, { "Il nome della rosa", "Umberto Eco" },
        { "Sostiene Pereira", "Antonio Tabucchi" }, { "Gli indifferenti", "Alberto Moravia" },
    };
    const int cols = 6, cw = 172, ch = 229, gx = 24, gy = 56, pad = 32;
    Canvas c;
    canvas_init(&c, pad * 2 + cols * cw + (cols - 1) * gx, pad * 2 + 5 * (ch + gy));
    canvas_clear(&c, G_CARTA);
    for (int i = 0; i < 30; i++) {
        int x = pad + (i % cols) * (cw + gx), y = pad + (i / cols) * (ch + gy);
        cover_draw(&c, x, y, cw, ch, B[i][0], B[i][1], NULL, 1);
        char info[96];
        cover_describe(B[i][0], B[i][1], NULL, info, sizeof info);
        font_draw_text(&c, FACE_MONO, 12, x, y + ch + 18, B[i][0], G_NERO, cw);
        font_draw_text(&c, FACE_MONO, 12, x, y + ch + 36, info, G_FUMO, cw);
    }
    int r = canvas_save_pgm(&c, path);
    free(c.px);
    return r;
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
            "  --copertine    mostra i modelli di copertina\n"
            "  --tavola FILE  salva una tavola di 30 copertine generate (PGM)\n"
            "Segnali: SIGUSR1 = il Kindle va in standby, SIGUSR2 = si risveglia.\n",
            CARBONELLA_VERSION);
}

int main(int argc, char **argv)
{
    App *a = xcalloc(1, sizeof *a);
    const char *sim = NULL, *keys = NULL, *books = NULL;
    int tasti = 0, ultimo = 0, sheet = 0;
    const char *board = NULL;
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
        else if (!strcmp(argv[i], "--tavola") && i + 1 < argc)
            board = argv[++i];
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
    a->standby_min = settings_int(&a->cfg, "standby_minuti", 5);
    g_battery_pct = settings_int(&a->cfg, "percentuale_batteria", 1) != 0;
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

    if (board) {
        int r = cover_board(board);
        input_close();
        display_close();
        return r ? 1 : 0;
    } else if (sheet) {
        cover_sheet(a);
    } else if (tasti) {
        key_test(a);
    } else {
        Next next = GO_LIBRARY;
        library_scan(&a->lib, a->books, a->data, a->base);
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
                    library_scan(&a->lib, a->books, a->data, a->base);
                    focus_path[0] = 0;
                }
                next = r < 0 ? GO_LIBRARY : (Next)r;
            } else if (next == GO_READER && g_selected) {
                a->entry = g_selected;
                snprintf(focus_path, sizeof focus_path, "%s", a->entry->path);
                next = run_reader(a);
                a->entry = NULL;
                if (next == GO_LIBRARY)
                    library_scan(&a->lib, a->books, a->data, a->base); /* order by last read */
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
