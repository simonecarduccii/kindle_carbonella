/*
 * Carbonella - page layout.
 *
 * A chapter is a list of blocks (paragraphs, headings...). We fill a page line by line:
 *   - greedy line breaking on spaces (and on soft hyphens, if the book has them);
 *   - justified text: the leftover space is spread over the spaces of the line;
 *   - first-line indent for paragraphs, no blank lines between them (like a printed book);
 *   - a heading never stays alone at the bottom of a page.
 * The same function measures (to paginate) and draws (to show a page), so the two never disagree.
 */
#include "carbonella.h"

#include <string.h>

WordList *layout_words;
const TextMark *layout_marks;
int layout_nmarks;

/* The mark covering byte i, 0 if none (a selection wins over a saved highlight). */
static int mark_at(uint32_t i)
{
    int k = 0;
    for (int m = 0; m < layout_nmarks; m++)
        if (i >= layout_marks[m].start && i < layout_marks[m].end && layout_marks[m].kind > k)
            k = layout_marks[m].kind;
    return k;
}

static void add_word(uint32_t start, uint32_t end, int x0, int x1, int top, int lh)
{
    WordList *wl = layout_words;
    if (!wl || end <= start)
        return;
    if (wl->n == wl->cap) {
        wl->cap = wl->cap ? wl->cap * 2 : 256;
        wl->w = xrealloc(wl->w, wl->cap * sizeof(WordBox));
    }
    wl->w[wl->n++] = (WordBox){ start, end, x0, x1, top, lh };
}

typedef struct {
    uint32_t start, end, next; /* text range drawn; where the next line starts */
    int width64;               /* natural width */
    int nspaces;               /* stretchable spaces in [start, end) */
    int last;                  /* last line of the block */
    int hyphen;                /* ends with a soft hyphen: draw "-" */
} Line;

typedef struct {
    int size, lh, before, after, align, bold; /* align: 0 justify, 1 left, 2 centre */
} BlockStyle;

static BlockStyle block_style(const LayoutParams *lp, const Block *bk)
{
    BlockStyle s = { lp->size, lp->size * lp->line_pct / 100, 0, 0, lp->justify ? 0 : 1, 0 };
    switch (bk->kind) {
    case BLK_HEADING1:
        s.size = lp->size * 145 / 100;
        s.lh = s.size * 130 / 100;
        s.before = s.lh * 2;
        s.after = s.lh;
        s.align = 2;
        s.bold = 1;
        break;
    case BLK_HEADING2:
        s.size = lp->size * 120 / 100;
        s.lh = s.size * 135 / 100;
        s.before = s.lh;
        s.after = s.lh / 2;
        s.align = 2;
        s.bold = 1;
        break;
    case BLK_CENTER:
        s.align = 2;
        break;
    case BLK_SEPARATOR:
        s.lh = lp->size * lp->line_pct / 100 * 2;
        break;
    default:
        break;
    }
    return s;
}

static Face face_of(uint8_t style, int bold);

/* ------------------------------------------------- Italian hyphenation
 *
 * Italian splits into syllables with a few regular rules, so a small algorithm does the job
 * without dictionaries (the same rules taught at school):
 *   - one consonant between vowels goes with the next syllable:   a-mo-re
 *   - double consonants are split:                                gat-to, ac-qua
 *   - s + consonant goes with the next syllable:                  pa-sta
 *   - l, m, n, r + consonant are split:                           al-to, cam-po, par-te
 *   - groups that can start a word stay together:                 ma-dre, o-gni, fio-chi
 *   - vowels next to each other are never split (diphthongs):     fio-re, Gio-van-ni
 * We never leave fewer than 2 letters on either side.
 */

static int fold(uint32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (int)c + 32;
    switch (c) {
    case 0xE0: case 0xE1: case 0xC0: case 0xC1: return 'a';
    case 0xE8: case 0xE9: case 0xC8: case 0xC9: return 'e';
    case 0xEC: case 0xED: case 0xCC: case 0xCD: return 'i';
    case 0xF2: case 0xF3: case 0xD2: case 0xD3: return 'o';
    case 0xF9: case 0xFA: case 0xD9: case 0xDA: return 'u';
    }
    return c < 128 ? (int)c : 0;
}

static int is_vowel(int c) { return c && strchr("aeiouy", c) != NULL; }
static int is_letter(int c) { return c >= 'a' && c <= 'z'; }

static int onset(int a, int b)
{
    static const char *ok[] = { "bl", "br", "cl", "cr", "dr", "fl", "fr", "gl", "gr", "pl", "pr", "tr", "vr",
                                "ch", "gh", "gn", "sc", "sp", "st", "sb", "sd", "sf", "sg", "sl", "sm", "sn",
                                "sv", "sr", "sq", "cq" };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++)
        if (ok[i][0] == a && ok[i][1] == b)
            return ok[i][0] != 'c' || ok[i][1] != 'q' ? 1 : 0;
    return 0;
}

void hyphen_points_it(const uint32_t *cps, int n, uint8_t *pts)
{
    memset(pts, 0, n);
    int L[64];
    if (n > 64)
        return;
    for (int i = 0; i < n; i++) {
        L[i] = fold(cps[i]);
        if (!is_letter(L[i]))
            return; /* only plain words */
    }
    /* 'u' after 'q' and 'i' in gli/sci/ci/gi before a vowel behave like consonants for splitting */
    int V[64];
    for (int i = 0; i < n; i++)
        V[i] = is_vowel(L[i]) && !(L[i] == 'u' && i > 0 && L[i - 1] == 'q');
    int i = 0;
    while (i < n && !V[i])
        i++;
    while (i < n) {
        while (i < n && V[i]) /* vowel group */
            i++;
        int a = i;
        while (i < n && !V[i]) /* consonant cluster */
            i++;
        int b = i;
        if (i >= n)
            break; /* final consonants stay */
        int len = b - a, cut;
        if (len == 0)
            continue;
        if (len == 1)
            cut = a;
        else if (L[a] == L[a + 1])
            cut = a + 1;
        else if (L[a] == 's')
            cut = a;
        else if (strchr("lmnr", L[a]))
            cut = a + 1;
        else if (onset(L[a], L[a + 1]))
            cut = a;
        else
            cut = a + 1;
        if (cut >= 2 && n - cut >= 2)
            pts[cut] = 1;
    }
}

/* Tries to split the word [ws, we) so that its first part plus "-" fits in 'room' (1/64 px).
 * Returns the byte where the split happens, or 0. *w64 gets the width of the first part + hyphen. */
static uint32_t try_hyphen(const Chapter *ch, uint32_t ws, uint32_t we, int size, int bold, int room, int *w64)
{
    const unsigned char *t = (const unsigned char *)ch->text;
    uint32_t cps[72], offs[72];
    int n = 0;
    for (uint32_t i = ws; i < we && n < 72;) {
        offs[n] = i;
        i += utf8_decode(t + i, we - i, &cps[n]);
        n++;
    }
    /* leading/trailing punctuation («, —, ., ...) is not part of the syllables */
    /* syllables only inside the last run of letters: "dell'amico," -> "amico" */
    int b = n;
    while (b > 0 && !is_letter(fold(cps[b - 1])))
        b--;
    int a = b;
    while (a > 0 && is_letter(fold(cps[a - 1])))
        a--;
    if (b - a < 4 || b - a > 64)
        return 0;
    uint8_t pts[64];
    hyphen_points_it(cps + a, b - a, pts);
    int best = 0, best_w = 0, x = 0;
    uint32_t prev = 0;
    for (int k = 0; k < b; k++) {
        Face f = face_of(ch->style[offs[k]], bold);
        if (k > a && pts[k - a]) {
            int hw = x + font_advance(f, size, prev, '-');
            if (hw <= room) {
                best = k;
                best_w = hw;
            } else
                break;
        }
        x += font_advance(f, size, prev, cps[k]);
        prev = cps[k];
    }
    if (!best)
        return 0;
    *w64 = best_w;
    return offs[best];
}

static Face face_of(uint8_t style, int bold)
{
    return (Face)((style & (ST_ITALIC | ST_BOLD)) | (bold ? ST_BOLD : 0));
}

static void break_line(const Chapter *ch, const Block *bk, const BlockStyle *bs, uint32_t pos, int avail,
                       int hyphenate, Line *out)
{
    const unsigned char *t = (const unsigned char *)ch->text;
    int avail64 = avail << 6;
    int x64 = 0, nspaces = 0;
    uint32_t prev = 0;
    int prev_face = -1;
    /* best break found so far */
    int have_break = 0;
    Line brk = { 0 };
    uint32_t i = pos;
    uint32_t word_start = pos;
    int word_x64 = 0, word_spaces = 0;
    memset(out, 0, sizeof *out);
    out->start = pos;
    while (i < bk->end) {
        uint32_t cp;
        int l = utf8_decode(t + i, bk->end - i, &cp);
        Face f = face_of(ch->style[i], bs->bold);
        if ((int)f != prev_face)
            prev = 0;
        if (cp == ' ') {
            /* break opportunity before this space */
            brk = (Line){ pos, i, i + 1, x64, nspaces, 0, 0 };
            have_break = 1;
            x64 += font_advance(f, bs->size, 0, ' ');
            nspaces++;
            prev = 0;
            prev_face = f;
            i += l;
            word_start = i;
            word_x64 = x64;
            word_spaces = nspaces;
            continue;
        }
        int adv = font_advance(f, bs->size, prev, cp);
        if (cp == 0xAD) {
            int hy = font_advance(f, bs->size, prev, '-');
            if (x64 + hy <= avail64) {
                brk = (Line){ pos, i, i + l, x64 + hy, nspaces, 0, 1 };
                have_break = 1;
            }
        }
        if (x64 + adv > avail64) {
            /* a break at the last space that leaves the line only a bit loose is good enough:
             * hyphenate only when the spaces would stretch more than 1/3 of the font size */
            int loose = !have_break || brk.nspaces == 0 ||
                        (avail64 - brk.width64) / brk.nspaces > (bs->size << 6) / 3;
            if (hyphenate && loose) {
                uint32_t we = i;
                while (we < bk->end && t[we] != ' ')
                    we++;
                int hw;
                uint32_t cut = try_hyphen(ch, word_start, we, bs->size, bs->bold, avail64 - word_x64, &hw);
                if (cut) {
                    *out = (Line){ pos, cut, cut, word_x64 + hw, word_spaces, 0, 1 };
                    return;
                }
            }
            if (have_break) {
                *out = brk;
                return;
            }
            if (i > pos) { /* a single word wider than the line: cut it */
                *out = (Line){ pos, i, i, x64, nspaces, 0, 0 };
                return;
            }
        }
        x64 += adv;
        prev = cp;
        prev_face = f;
        i += l;
    }
    *out = (Line){ pos, bk->end, bk->end, x64, nspaces, 1, 0 };
}

static void draw_line(Canvas *c, const Chapter *ch, const BlockStyle *bs, const Line *ln, int x, int avail,
                      int baseline, int top)
{
    const unsigned char *t = (const unsigned char *)ch->text;
    int x64 = x << 6;
    int extra64 = 0, rem = 0;
    if (bs->align == 2)
        x64 += ((avail << 6) - ln->width64) / 2;
    else if (bs->align == 0 && !ln->last && ln->nspaces > 0) {
        int slack = (avail << 6) - ln->width64;
        if (slack > 0 && slack / ln->nspaces <= (bs->size << 6)) { /* too loose: leave it ragged */
            extra64 = slack / ln->nspaces;
            rem = slack % ln->nspaces;
        }
    }
    uint32_t prev = 0;
    int prev_face = -1;
    Face f = FACE_REGULAR;
    uint32_t wstart = ln->start;
    int wx = x64 >> 6, lh = bs->lh;
    for (uint32_t i = ln->start; i < ln->end;) {
        uint32_t cp;
        int l = utf8_decode(t + i, ln->end - i, &cp);
        f = face_of(ch->style[i], bs->bold);
        if ((int)f != prev_face)
            prev = 0;
        int mk = layout_nmarks ? mark_at(i) : 0;
        int x0 = x64 >> 6;
        if (cp == ' ') {
            add_word(wstart, i, wx, x0, top, lh);
            x64 += font_advance(f, bs->size, 0, ' ') + extra64 + (rem > 0 ? 1 : 0);
            if (rem > 0)
                rem--;
            prev = 0;
            wstart = i + l;
            wx = x64 >> 6;
            /* a space inside a mark joins the two words */
            if (mk == MARK_SELECTION)
                canvas_fill(c, x0, top + 3, (x64 >> 6) - x0, lh - 5, G_GRAFITE);
            else if (mk == MARK_SAVED)
                canvas_fill(c, x0, top + lh - 2, (x64 >> 6) - x0, 2, G_NERO);
        } else {
            int adv = font_advance(f, bs->size, prev, cp);
            int kern = adv - font_advance(f, bs->size, 0, cp);
            uint8_t ink = INK_BLACK;
            if (mk) {
                /* Figma "Evidenzia 2/3": the box reaches a little past the first and the last letter (4 px in Figma,
                 * 2 here: a tight justified space must not cover the next word) */
                int pad_l = i == 0 || !mark_at(i - 1) ? 2 : 0;
                int pad_r = !mark_at(i + l) ? 2 : 0;
                int w = ((x64 + adv) >> 6) - x0 + 1;
                if (mk == MARK_SELECTION) {
                    canvas_fill(c, x0 - pad_l, top + 3, w + pad_l + pad_r, lh - 5, G_GRAFITE);
                    ink = G_CARTA;
                } else
                    canvas_fill(c, x0 - pad_l, top + lh - 2, w + pad_l + pad_r, 2, G_NERO);
            }
            font_draw(c, f, bs->size, x64 + kern, baseline, cp, ink);
            x64 += adv;
            prev = cp;
        }
        prev_face = f;
        i += l;
    }
    add_word(wstart, ln->end, wx, x64 >> 6, top, lh);
    if (ln->hyphen)
        font_draw(c, f, bs->size, x64, baseline, '-', INK_BLACK);
}

TextPos layout_page(const LayoutParams *lp, const Chapter *ch, TextPos from, Canvas *c)
{
    const int top = lp->margin_top, bottom = lp->height - lp->margin_bottom;
    const int width = lp->width - 2 * lp->margin_x;
    int y = top;
    TextPos p = from;
    while (p.block < ch->nblocks) {
        const Block *bk = &ch->blocks[p.block];
        BlockStyle bs = block_style(lp, bk);
        int fresh = p.pos == bk->start;
        if (fresh && y > top) {
            /* keep headings with what follows: heading + 2 body lines must fit */
            if (bk->kind == BLK_HEADING1 || bk->kind == BLK_HEADING2) {
                int body_lh = lp->size * lp->line_pct / 100;
                if (y + bs.before + bs.lh + bs.after + 2 * body_lh > bottom)
                    break;
            }
            y += bs.before;
        } else if (fresh && y == top && bk->kind == BLK_HEADING1) {
            y += bs.lh; /* chapter titles start a little lower */
        }
        if (bk->kind == BLK_SEPARATOR) {
            if (y + bs.lh > bottom)
                break;
            if (c)
                canvas_hline(c, lp->margin_x + width / 2 - 30, y + bs.lh / 2 - 1, 60, 2, INK_DARK);
            y += bs.lh;
            p.block++;
            if (p.block < ch->nblocks)
                p.pos = ch->blocks[p.block].start;
            continue;
        }
        int base = ui_baseline(FACE_REGULAR, bs.size, 0, bs.lh); /* baseline inside the line box, as CSS */
        int hyph_run = 0;                                         /* consecutive hyphenated lines */
        int done = 0;
        while (p.pos < bk->end || (fresh && bk->start == bk->end)) {
            if (y + bs.lh > bottom) {
                done = 1;
                break;
            }
            int indent = 0;
            if (p.pos == bk->start && bk->kind == BLK_PARA)
                indent = lp->indent;
            Line ln;
            break_line(ch, bk, &bs, p.pos, width - indent, lp->hyphenate && bs.align != 2 && hyph_run < 2, &ln);
            hyph_run = ln.hyphen ? hyph_run + 1 : 0;
            if (c)
                draw_line(c, ch, &bs, &ln, lp->margin_x + indent, width - indent, y + base, y);
            y += bs.lh;
            p.pos = ln.next;
            fresh = 0;
            if (ln.last || ln.next >= bk->end)
                break;
        }
        if (done)
            break;
        y += bs.after;
        p.block++;
        if (p.block < ch->nblocks)
            p.pos = ch->blocks[p.block].start;
    }
    if (p.block >= ch->nblocks) {
        p.block = ch->nblocks;
        p.pos = ch->len;
    }
    return p;
}

void paginate(const LayoutParams *lp, const Chapter *ch, Pagination *out)
{
    out->n = 0;
    TextPos p = { 0, 0 };
    if (ch->nblocks > 0)
        p.pos = ch->blocks[0].start;
    do {
        if (out->n == out->cap) {
            out->cap = out->cap ? out->cap * 2 : 64;
            out->starts = xrealloc(out->starts, out->cap * sizeof(TextPos));
        }
        out->starts[out->n++] = p;
        TextPos next = layout_page(lp, ch, p, NULL);
        if (next.block == p.block && next.pos == p.pos) {
            /* nothing fit (should not happen): skip the block to avoid an endless loop */
            next.block = p.block + 1;
            next.pos = next.block < ch->nblocks ? ch->blocks[next.block].start : ch->len;
        }
        p = next;
    } while (p.block < ch->nblocks);
}

int pagination_find(const Pagination *pg, uint32_t pos)
{
    int lo = 0, hi = pg->n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (pg->starts[mid].pos <= pos)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}
