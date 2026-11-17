/*
 * Carbonella - books.
 *
 * An EPUB is a ZIP file with XHTML chapters inside:
 *   META-INF/container.xml  -> says where the OPF file is
 *   *.opf                   -> title, author, list of files (manifest), reading order (spine)
 *   toc.ncx / nav.xhtml     -> table of contents
 * We read the ZIP with miniz and parse the XML/XHTML with a small hand-written scanner: books need
 * only paragraphs, headings, italics and bold, so a full HTML/CSS engine would be wasted memory.
 *
 * TXT files are split into chunks of about 64 KB so that pagination stays fast.
 */
#include "carbonella.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <strings.h>

#include "../third_party/miniz/miniz.h"

/* ------------------------------------------------------------ XML scanner */

typedef enum { XT_END, XT_TEXT, XT_OPEN, XT_CLOSE } XmlType;

typedef struct {
    XmlType type;
    const char *s; /* text, or tag name (without namespace prefix) */
    size_t len;
    const char *attrs; /* raw attributes of an OPEN tag */
    size_t attrs_len;
    int self_close;
} XmlTok;

typedef struct {
    const char *p, *end;
} Xml;

static int name_is(const XmlTok *t, const char *name)
{
    size_t n = strlen(name);
    return t->len == n && strncasecmp(t->s, name, n) == 0;
}

static const char *find(const char *p, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (; p + n <= end; p++)
        if (memcmp(p, needle, n) == 0)
            return p;
    return NULL;
}

static int xml_next(Xml *x, XmlTok *t)
{
    memset(t, 0, sizeof *t);
    if (x->p >= x->end)
        return t->type = XT_END;
    if (*x->p != '<') {
        const char *q = memchr(x->p, '<', x->end - x->p);
        if (!q)
            q = x->end;
        t->type = XT_TEXT;
        t->s = x->p;
        t->len = q - x->p;
        x->p = q;
        return XT_TEXT;
    }
    const char *p = x->p + 1;
    if (p < x->end && (*p == '!' || *p == '?')) {
        const char *close = ">";
        if (x->end - p >= 3 && memcmp(p, "!--", 3) == 0)
            close = "-->";
        else if (x->end - p >= 8 && memcmp(p, "![CDATA[", 8) == 0) {
            const char *q = find(p, x->end, "]]>");
            t->type = XT_TEXT;
            t->s = p + 8;
            t->len = (q ? q : x->end) - t->s;
            x->p = q ? q + 3 : x->end;
            return XT_TEXT;
        }
        const char *q = find(p, x->end, close);
        x->p = q ? q + strlen(close) : x->end;
        return xml_next(x, t);
    }
    int closing = 0;
    if (p < x->end && *p == '/') {
        closing = 1;
        p++;
    }
    const char *name = p;
    while (p < x->end && !isspace((unsigned char)*p) && *p != '>' && *p != '/')
        p++;
    const char *name_end = p;
    /* drop namespace prefix (dc:title -> title) */
    for (const char *q = name; q < name_end; q++)
        if (*q == ':')
            name = q + 1;
    const char *attrs = p;
    char quote = 0;
    while (p < x->end && (quote || *p != '>')) {
        if (quote && *p == quote)
            quote = 0;
        else if (!quote && (*p == '"' || *p == '\''))
            quote = *p;
        p++;
    }
    t->s = name;
    t->len = name_end - name;
    t->attrs = attrs;
    t->attrs_len = p - attrs;
    t->self_close = p > attrs && p[-1] == '/';
    t->type = closing ? XT_CLOSE : XT_OPEN;
    x->p = p < x->end ? p + 1 : x->end;
    return t->type;
}

/* Copies attribute 'name' (namespace prefix ignored) into out. Returns 1 if found. */
static int xml_attr(const XmlTok *t, const char *name, char *out, size_t outsz)
{
    const char *p = t->attrs, *end = t->attrs + t->attrs_len;
    size_t n = strlen(name);
    while (p < end) {
        while (p < end && (isspace((unsigned char)*p) || *p == '/'))
            p++;
        const char *an = p;
        while (p < end && *p != '=' && !isspace((unsigned char)*p))
            p++;
        const char *an_end = p;
        for (const char *q = an; q < an_end; q++)
            if (*q == ':')
                an = q + 1;
        while (p < end && isspace((unsigned char)*p))
            p++;
        if (p >= end || *p != '=')
            continue;
        p++;
        while (p < end && isspace((unsigned char)*p))
            p++;
        if (p >= end)
            break;
        char q = *p;
        const char *v, *v_end;
        if (q == '"' || q == '\'') {
            v = ++p;
            while (p < end && *p != q)
                p++;
            v_end = p;
            if (p < end)
                p++;
        } else {
            v = p;
            while (p < end && !isspace((unsigned char)*p))
                p++;
            v_end = p;
        }
        if ((size_t)(an_end - an) == n && strncasecmp(an, name, n) == 0) {
            size_t l = v_end - v;
            if (l >= outsz)
                l = outsz - 1;
            memcpy(out, v, l);
            out[l] = 0;
            return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------- entities */

static const struct {
    const char *name;
    uint32_t cp;
} ENTITIES[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
    { "nbsp", 0xA0 }, { "shy", 0xAD }, { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
    { "laquo", 0xAB }, { "raquo", 0xBB }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C },
    { "rdquo", 0x201D }, { "bdquo", 0x201E }, { "sbquo", 0x201A }, { "copy", 0xA9 }, { "reg", 0xAE },
    { "trade", 0x2122 }, { "deg", 0xB0 }, { "middot", 0xB7 }, { "bull", 0x2022 }, { "euro", 0x20AC },
    { "sect", 0xA7 }, { "para", 0xB6 }, { "times", 0xD7 }, { "thinsp", 0x2009 }, { "ensp", 0x2002 },
    { "emsp", 0x2003 }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "ordf", 0xAA }, { "ordm", 0xBA },
    { "agrave", 0xE0 }, { "aacute", 0xE1 }, { "acirc", 0xE2 }, { "auml", 0xE4 }, { "egrave", 0xE8 },
    { "eacute", 0xE9 }, { "ecirc", 0xEA }, { "euml", 0xEB }, { "igrave", 0xEC }, { "iacute", 0xED },
    { "icirc", 0xEE }, { "iuml", 0xEF }, { "ograve", 0xF2 }, { "oacute", 0xF3 }, { "ocirc", 0xF4 },
    { "ouml", 0xF6 }, { "ugrave", 0xF9 }, { "uacute", 0xFA }, { "ucirc", 0xFB }, { "uuml", 0xFC },
    { "Agrave", 0xC0 }, { "Aacute", 0xC1 }, { "Egrave", 0xC8 }, { "Eacute", 0xC9 }, { "Igrave", 0xCC },
    { "Ograve", 0xD2 }, { "Ugrave", 0xD9 }, { "ccedil", 0xE7 }, { "Ccedil", 0xC7 }, { "ntilde", 0xF1 },
    { "szlig", 0xDF }, { "oelig", 0x153 }, { "aelig", 0xE6 },
};

/* Decodes the entity starting at s ('&'). Returns length consumed, 0 if not an entity. */
static size_t decode_entity(const char *s, const char *end, uint32_t *cp)
{
    const char *semi = memchr(s, ';', end - s > 12 ? 12 : end - s);
    if (!semi)
        return 0;
    if (s[1] == '#') {
        char *e;
        unsigned long v = (s[2] == 'x' || s[2] == 'X') ? strtoul(s + 3, &e, 16) : strtoul(s + 2, &e, 10);
        if (e != semi || v == 0 || v > 0x10FFFF)
            return 0;
        *cp = (uint32_t)v;
        return semi - s + 1;
    }
    size_t n = semi - s - 1;
    for (size_t i = 0; i < sizeof ENTITIES / sizeof ENTITIES[0]; i++)
        if (strlen(ENTITIES[i].name) == n && memcmp(ENTITIES[i].name, s + 1, n) == 0) {
            *cp = ENTITIES[i].cp;
            return n + 2;
        }
    return 0;
}

/* Text of an element as a trimmed, entity-decoded, whitespace-collapsed string. */
static char *clean_text(const char *s, size_t len)
{
    char *o = xmalloc(len * 2 + 1);
    size_t j = 0;
    int space = 1;
    for (size_t i = 0; i < len;) {
        char c = s[i];
        if (c == '&') {
            uint32_t cp;
            size_t l = decode_entity(s + i, s + len, &cp);
            if (l) {
                if (cp == 0xA0)
                    cp = ' ';
                if (cp == ' ') {
                    if (!space)
                        o[j++] = ' ';
                    space = 1;
                } else {
                    j += utf8_encode(cp, o + j);
                    space = 0;
                }
                i += l;
                continue;
            }
        }
        if (isspace((unsigned char)c)) {
            if (!space)
                o[j++] = ' ';
            space = 1;
        } else {
            o[j++] = c;
            space = 0;
        }
        i++;
    }
    while (j > 0 && o[j - 1] == ' ')
        j--;
    o[j] = 0;
    return o;
}

/* --------------------------------------------------------------- paths */

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else
            *o++ = *s;
    }
    *o = 0;
}

/* Joins a base directory ("OEBPS/") and a relative href, resolving "../" and dropping "#frag". */
static char *resolve(const char *base_dir, const char *href)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s%s", href[0] == '/' ? "" : base_dir, href[0] == '/' ? href + 1 : href);
    char *hash = strchr(buf, '#');
    if (hash)
        *hash = 0;
    url_decode(buf);
    /* normalise segments */
    char *parts[64];
    int n = 0;
    char *save = NULL;
    for (char *seg = strtok_r(buf, "/", &save); seg && n < 64; seg = strtok_r(NULL, "/", &save)) {
        if (strcmp(seg, ".") == 0)
            continue;
        if (strcmp(seg, "..") == 0) {
            if (n > 0)
                n--;
            continue;
        }
        parts[n++] = seg;
    }
    char out[1024];
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i)
            strncat(out, "/", sizeof out - strlen(out) - 1);
        strncat(out, parts[i], sizeof out - strlen(out) - 1);
    }
    return xstrdup(out);
}

static char *dir_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    size_t n = slash ? (size_t)(slash - path + 1) : 0;
    char *d = xmalloc(n + 1);
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

/* ----------------------------------------------------------- zip access */

static char *zip_read(mz_zip_archive *z, const char *name, size_t *len)
{
    size_t n = 0;
    int idx = mz_zip_reader_locate_file(z, name, NULL, 0);
    if (idx < 0)
        return NULL;
    char *raw = mz_zip_reader_extract_to_heap(z, (mz_uint)idx, &n, 0);
    if (!raw)
        return NULL;
    char *s = xmalloc(n + 1);
    memcpy(s, raw, n);
    s[n] = 0;
    mz_free(raw);
    if (len)
        *len = n;
    return s;
}

static uint32_t zip_size(mz_zip_archive *z, const char *name)
{
    int idx = mz_zip_reader_locate_file(z, name, NULL, 0);
    mz_zip_archive_file_stat st;
    if (idx < 0 || !mz_zip_reader_file_stat(z, (mz_uint)idx, &st))
        return 0;
    return (uint32_t)st.m_uncomp_size;
}

/* ------------------------------------------------------------ OPF parse */

typedef struct {
    char *id, *href, *type, *props;
} Item;

/* v1.0.2: store titles often end with edition notes, e.g. "Così parlò Zarathustra (Piccola biblioteca
 * Adelphi) (Italian Edition)" or "La morte della Pizia (Adelphi)": trailing groups in parentheses are
 * dropped, unless that would leave nothing. */
void book_clean_title(char *t)
{
    if (!t)
        return;
    for (;;) {
        size_t n = strlen(t);
        while (n > 0 && t[n - 1] == ' ')
            t[--n] = 0;
        if (n == 0 || t[n - 1] != ')')
            return;
        char *open = strrchr(t, '(');
        if (!open)
            return;
        char *end = open;
        while (end > t && end[-1] == ' ')
            end--;
        if (end == t)
            return;
        *end = 0;
    }
}

typedef struct {
    char *title, *author, *language, *publisher;
    Item *items;
    int nitems;
    char **spine_ids;
    int nspine;
    char *toc_id; /* NCX id from <spine toc="..."> */
} Opf;

static void opf_free(Opf *o)
{
    free(o->title);
    free(o->author);
    free(o->language);
    free(o->publisher);
    for (int i = 0; i < o->nitems; i++) {
        free(o->items[i].id);
        free(o->items[i].href);
        free(o->items[i].type);
        free(o->items[i].props);
    }
    free(o->items);
    for (int i = 0; i < o->nspine; i++)
        free(o->spine_ids[i]);
    free(o->spine_ids);
    free(o->toc_id);
}

/* Reads the text up to the closing tag of the element just opened. */
static char *element_text(Xml *x, const char *tag)
{
    const char *start = x->p;
    XmlTok t;
    const char *end = start;
    int depth = 0;
    for (;;) {
        const char *before = x->p;
        int ty = xml_next(x, &t);
        if (ty == XT_END) {
            end = x->p;
            break;
        }
        if (ty == XT_OPEN && !t.self_close)
            depth++;
        if (ty == XT_CLOSE) {
            if (depth == 0 && name_is(&t, tag)) {
                end = before;
                break;
            }
            depth--;
        }
    }
    /* strip inner tags */
    char *raw = xmalloc(end - start + 1);
    size_t j = 0;
    Xml in = { start, end };
    while (xml_next(&in, &t) != XT_END)
        if (t.type == XT_TEXT) {
            memcpy(raw + j, t.s, t.len);
            j += t.len;
        }
    char *clean = clean_text(raw, j);
    free(raw);
    return clean;
}

static int parse_opf(const char *data, size_t len, Opf *o, int meta_only)
{
    memset(o, 0, sizeof *o);
    Xml x = { data, data + len };
    XmlTok t;
    int cap_items = 0, cap_spine = 0;
    while (xml_next(&x, &t) != XT_END) {
        if (t.type != XT_OPEN)
            continue;
        if (name_is(&t, "title") && !o->title && !t.self_close) {
            o->title = element_text(&x, "title");
            book_clean_title(o->title);
        } else if (name_is(&t, "creator") && !t.self_close) {
            char role[32] = "";
            xml_attr(&t, "role", role, sizeof role);
            char *who = element_text(&x, "creator");
            if (!o->author && (!role[0] || strcmp(role, "aut") == 0))
                o->author = who;
            else
                free(who);
        } else if (name_is(&t, "publisher") && !o->publisher && !t.self_close) {
            o->publisher = element_text(&x, "publisher");
        } else if (name_is(&t, "language") && !o->language && !t.self_close) {
            o->language = element_text(&x, "language");
        } else if (meta_only && name_is(&t, "manifest")) {
            break;
        } else if (name_is(&t, "item")) {
            char id[256] = "", href[1024] = "", type[128] = "", props[256] = "";
            xml_attr(&t, "id", id, sizeof id);
            xml_attr(&t, "href", href, sizeof href);
            xml_attr(&t, "media-type", type, sizeof type);
            xml_attr(&t, "properties", props, sizeof props);
            if (o->nitems == cap_items) {
                cap_items = cap_items ? cap_items * 2 : 64;
                o->items = xrealloc(o->items, cap_items * sizeof(Item));
            }
            o->items[o->nitems++] = (Item){ xstrdup(id), xstrdup(href), xstrdup(type), xstrdup(props) };
        } else if (name_is(&t, "spine")) {
            char toc[256];
            if (xml_attr(&t, "toc", toc, sizeof toc))
                o->toc_id = xstrdup(toc);
        } else if (name_is(&t, "itemref")) {
            char idref[256] = "", linear[16] = "";
            xml_attr(&t, "idref", idref, sizeof idref);
            xml_attr(&t, "linear", linear, sizeof linear);
            if (strcmp(linear, "no") == 0 || !idref[0])
                continue;
            if (o->nspine == cap_spine) {
                cap_spine = cap_spine ? cap_spine * 2 : 64;
                o->spine_ids = xrealloc(o->spine_ids, cap_spine * sizeof(char *));
            }
            o->spine_ids[o->nspine++] = xstrdup(idref);
        }
    }
    return 0;
}

static Item *opf_item(Opf *o, const char *id)
{
    for (int i = 0; i < o->nitems; i++)
        if (strcmp(o->items[i].id, id) == 0)
            return &o->items[i];
    return NULL;
}

static char *opf_path(mz_zip_archive *z)
{
    char *container = zip_read(z, "META-INF/container.xml", NULL);
    if (!container)
        return NULL;
    Xml x = { container, container + strlen(container) };
    XmlTok t;
    char path[1024] = "";
    while (xml_next(&x, &t) != XT_END)
        if (t.type == XT_OPEN && name_is(&t, "rootfile") && xml_attr(&t, "full-path", path, sizeof path))
            break;
    free(container);
    if (!path[0])
        return NULL;
    url_decode(path);
    return xstrdup(path);
}

static char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    char *n = xstrdup(s);
    char *dot = strrchr(n, '.');
    if (dot && dot != n)
        *dot = 0;
    for (char *p = n; *p; p++)
        if (*p == '_')
            *p = ' ';
    return n;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

/* v1.1: pages of the printed edition, from the EPUB's own page list (EPUB 3 nav "page-list" or the
 * NCX <pageList>), which publishers fill with the page breaks of the paper book. Returns the highest
 * arabic page number, or the number of entries if none is numeric; 0 if the book has no page list. */
static int page_list_pages(const char *data, size_t len, int is_nav)
{
    Xml x = { data, data + len };
    XmlTok t;
    int in_list = 0, depth = 0, count = 0, maxnum = 0;
    while (xml_next(&x, &t) != XT_END) {
        if (is_nav) {
            if (t.type == XT_OPEN && name_is(&t, "nav") && !t.self_close) {
                char type[64] = "";
                xml_attr(&t, "type", type, sizeof type);
                depth++;
                if (strstr(type, "page-list"))
                    in_list = depth;
            } else if (t.type == XT_CLOSE && name_is(&t, "nav")) {
                if (in_list == depth)
                    in_list = 0;
                depth--;
            } else if (in_list && t.type == XT_OPEN && name_is(&t, "a") && !t.self_close) {
                char *label = element_text(&x, "a");
                int v = label ? atoi(label) : 0;
                count++;
                if (v > maxnum && v < 20000)
                    maxnum = v;
                free(label);
            }
        } else {
            if (t.type == XT_OPEN && name_is(&t, "pageList"))
                in_list = 1;
            else if (t.type == XT_CLOSE && name_is(&t, "pageList"))
                in_list = 0;
            else if (in_list && t.type == XT_OPEN && name_is(&t, "pageTarget")) {
                char type[32] = "", value[32] = "";
                xml_attr(&t, "type", type, sizeof type);
                xml_attr(&t, "value", value, sizeof value);
                int v = atoi(value);
                count++;
                if ((!type[0] || !strcasecmp(type, "normal")) && v > maxnum && v < 20000)
                    maxnum = v;
            }
        }
    }
    return maxnum > 0 ? maxnum : count;
}

int book_peek_meta(const char *path, char **title, char **author, char **publisher, long long *text_bytes,
                   int *print_pages)
{
    *title = *author = *publisher = NULL;
    *text_bytes = 0;
    *print_pages = 0;
    if (ends_with(path, ".txt")) {
        *title = base_name(path);
        *author = xstrdup("");
        *publisher = xstrdup("");
        struct stat st;
        if (stat(path, &st) == 0)
            *text_bytes = st.st_size;
        return 0;
    }
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, path, 0))
        return -1;
    int rc = -1;
    char *opf = opf_path(&z);
    size_t len;
    char *data = opf ? zip_read(&z, opf, &len) : NULL;
    if (data) {
        Opf o;
        parse_opf(data, len, &o, 0); /* the manifest too: it names the nav / NCX with the page list */
        *title = o.title && *o.title ? xstrdup(o.title) : base_name(path);
        *author = xstrdup(o.author ? o.author : "");
        *publisher = xstrdup(o.publisher ? o.publisher : "");
        char *odir = dir_of(opf);
        for (int i = 0; i < o.nitems && !*print_pages; i++) {
            int is_nav = strstr(o.items[i].props, "nav") != NULL;
            if (!is_nav && strcmp(o.items[i].type, "application/x-dtbncx+xml") != 0)
                continue;
            char *p = resolve(odir, o.items[i].href);
            size_t nl;
            char *d = zip_read(&z, p, &nl);
            if (d)
                *print_pages = page_list_pages(d, nl, is_nav);
            free(d);
            free(p);
        }
        free(odir);
        opf_free(&o);
        rc = 0;
    }
    /* text size: all the (X)HTML files, read from the zip directory (no decompression) */
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); i++) {
        mz_zip_archive_file_stat fs;
        if (mz_zip_reader_file_stat(&z, i, &fs) &&
            (ends_with(fs.m_filename, ".xhtml") || ends_with(fs.m_filename, ".html") || ends_with(fs.m_filename, ".htm")))
            *text_bytes += (long long)fs.m_uncomp_size;
    }
    free(data);
    free(opf);
    mz_zip_reader_end(&z);
    return rc;
}

/* ------------------------------------------------------------------ TOC */

static int spine_index_of(Book *b, const char *path)
{
    for (int i = 0; i < b->nspine; i++)
        if (strcmp(b->spine_href[i], path) == 0)
            return i;
    return -1;
}

static void toc_add(Book *b, int *cap, char *title, int spine)
{
    if (spine < 0 || !title || !*title) {
        free(title);
        return;
    }
    if (b->ntoc == *cap) {
        *cap = *cap ? *cap * 2 : 32;
        b->toc = xrealloc(b->toc, *cap * sizeof(TocEntry));
    }
    b->toc[b->ntoc++] = (TocEntry){ title, spine };
}

static void parse_ncx(Book *b, const char *data, size_t len, const char *dir)
{
    Xml x = { data, data + len };
    XmlTok t;
    int cap = 0;
    char *label = NULL;
    while (xml_next(&x, &t) != XT_END) {
        if (t.type != XT_OPEN)
            continue;
        if (name_is(&t, "text") && !t.self_close) {
            free(label);
            label = element_text(&x, "text");
        } else if (name_is(&t, "content")) {
            char src[1024];
            if (xml_attr(&t, "src", src, sizeof src) && label) {
                char *p = resolve(dir, src);
                toc_add(b, &cap, label, spine_index_of(b, p));
                free(p);
                label = NULL;
            }
        } else if (name_is(&t, "pageList")) {
            break; /* page numbers: not a table of contents */
        }
    }
    free(label);
}

static void parse_nav(Book *b, const char *data, size_t len, const char *dir)
{
    Xml x = { data, data + len };
    XmlTok t;
    int cap = 0, in_toc = 0, nav_depth = 0;
    while (xml_next(&x, &t) != XT_END) {
        if (t.type == XT_OPEN && name_is(&t, "nav")) {
            char type[64] = "";
            xml_attr(&t, "type", type, sizeof type);
            nav_depth++;
            if (strcmp(type, "toc") == 0)
                in_toc = nav_depth;
        } else if (t.type == XT_CLOSE && name_is(&t, "nav")) {
            if (in_toc == nav_depth)
                in_toc = 0;
            nav_depth--;
        } else if (in_toc && t.type == XT_OPEN && name_is(&t, "a") && !t.self_close) {
            char href[1024] = "";
            xml_attr(&t, "href", href, sizeof href);
            char *label = element_text(&x, "a");
            char *p = resolve(dir, href);
            toc_add(b, &cap, label, spine_index_of(b, p));
            free(p);
        }
    }
}

/* ---------------------------------------------------------------- TXT */

#define TXT_CHUNK 65536

typedef struct {
    char *data;
    size_t len;
    uint32_t *starts; /* chunk start offsets, nspine+1 entries */
    int wrapped;      /* paragraphs separated by blank lines, single newlines are just wrapping */
} TxtData;

static int open_txt(Book *b)
{
    size_t len;
    char *raw = read_file(b->path, &len);
    if (!raw)
        return -1;
    TxtData *t = xcalloc(1, sizeof *t);
    if (len >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        memmove(raw, raw + 3, len - 3);
        len -= 3;
        raw[len] = 0;
    }
    if (!utf8_valid(raw, len)) {
        size_t l2;
        char *u = latin1_to_utf8(raw, len, &l2);
        free(raw);
        raw = u;
        len = l2;
    }
    t->data = raw;
    t->len = len;
    size_t lines = 0, blanks = 0;
    for (size_t i = 0; i < len; i++)
        if (raw[i] == '\n') {
            lines++;
            if (i + 1 < len && (raw[i + 1] == '\n' || (raw[i + 1] == '\r' && i + 2 < len && raw[i + 2] == '\n')))
                blanks++;
        }
    t->wrapped = lines > 0 && blanks * 10 > lines; /* many blank lines -> hard-wrapped text */
    int cap = 16;
    t->starts = xmalloc(cap * sizeof(uint32_t));
    int n = 0;
    t->starts[n++] = 0;
    size_t pos = 0;
    while (pos + TXT_CHUNK < len) {
        /* cut at a paragraph boundary after TXT_CHUNK bytes */
        const char *cut = find(raw + pos + TXT_CHUNK, raw + len, t->wrapped ? "\n\n" : "\n");
        if (!cut)
            break;
        pos = cut - raw + 1;
        if (n + 2 > cap) {
            cap *= 2;
            t->starts = xrealloc(t->starts, cap * sizeof(uint32_t));
        }
        t->starts[n++] = (uint32_t)pos;
    }
    t->starts[n] = (uint32_t)len;
    b->nspine = n;
    b->spine_href = xcalloc(n, sizeof(char *));
    b->spine_size = xcalloc(n, sizeof(uint32_t));
    for (int i = 0; i < n; i++) {
        b->spine_href[i] = xstrdup("txt");
        b->spine_size[i] = t->starts[i + 1] - t->starts[i];
        b->total_size += b->spine_size[i];
    }
    b->zip = t;
    b->is_txt = 1;
    b->title = base_name(b->path);
    b->author = xstrdup("");
    return 0;
}

/* ------------------------------------------------------------- open book */

Book *book_open(const char *path)
{
    Book *b = xcalloc(1, sizeof *b);
    b->path = xstrdup(path);
    if (ends_with(path, ".txt")) {
        if (open_txt(b) != 0) {
            book_close(b);
            return NULL;
        }
        return b;
    }
    mz_zip_archive *z = xcalloc(1, sizeof *z);
    b->zip = z;
    if (!mz_zip_reader_init_file(z, path, 0)) {
        log_msg("not a zip: %s", path);
        book_close(b);
        return NULL;
    }
    char *opf = opf_path(z);
    size_t len;
    char *data = opf ? zip_read(z, opf, &len) : NULL;
    if (!data) {
        log_msg("no OPF in %s", path);
        free(opf);
        book_close(b);
        return NULL;
    }
    Opf o;
    parse_opf(data, len, &o, 0);
    free(data);
    char *dir = dir_of(opf);
    b->title = o.title && *o.title ? xstrdup(o.title) : base_name(path);
    b->author = xstrdup(o.author ? o.author : "");
    b->language = xstrdup(o.language ? o.language : "");
    b->spine_href = xcalloc(o.nspine, sizeof(char *));
    b->spine_size = xcalloc(o.nspine, sizeof(uint32_t));
    for (int i = 0; i < o.nspine; i++) {
        Item *it = opf_item(&o, o.spine_ids[i]);
        if (!it)
            continue;
        char *p = resolve(dir, it->href);
        b->spine_href[b->nspine] = p;
        b->spine_size[b->nspine] = zip_size(z, p);
        b->total_size += b->spine_size[b->nspine];
        b->nspine++;
    }
    /* table of contents: EPUB 3 nav first, then EPUB 2 NCX */
    Item *nav = NULL, *ncx = NULL;
    for (int i = 0; i < o.nitems; i++) {
        if (strstr(o.items[i].props, "nav"))
            nav = &o.items[i];
        if (strcmp(o.items[i].type, "application/x-dtbncx+xml") == 0)
            ncx = &o.items[i];
    }
    if (o.toc_id && opf_item(&o, o.toc_id))
        ncx = opf_item(&o, o.toc_id);
    for (int pass = 0; pass < 2 && b->ntoc == 0; pass++) {
        Item *it = pass == 0 ? nav : ncx;
        if (!it)
            continue;
        char *p = resolve(dir, it->href);
        char *d = zip_read(z, p, &len);
        if (d) {
            char *pdir = dir_of(p);
            if (pass == 0)
                parse_nav(b, d, len, pdir);
            else
                parse_ncx(b, d, len, pdir);
            free(pdir);
            free(d);
        }
        free(p);
    }
    log_msg("book \"%s\" by \"%s\": %d spine items, %d toc entries, %llu bytes", b->title, b->author,
            b->nspine, b->ntoc, (unsigned long long)b->total_size);
    free(dir);
    free(opf);
    opf_free(&o);
    if (b->nspine == 0) {
        book_close(b);
        return NULL;
    }
    return b;
}

void book_close(Book *b)
{
    if (!b)
        return;
    if (b->zip) {
        if (b->is_txt) {
            TxtData *t = b->zip;
            free(t->data);
            free(t->starts);
            free(t);
        } else {
            mz_zip_reader_end(b->zip);
            free(b->zip);
        }
    }
    for (int i = 0; i < b->nspine; i++)
        free(b->spine_href[i]);
    for (int i = 0; i < b->ntoc; i++)
        free(b->toc[i].title);
    free(b->toc);
    free(b->spine_href);
    free(b->spine_size);
    free(b->title);
    free(b->author);
    free(b->language);
    free(b->path);
    free(b);
}

/* ------------------------------------------------------- chapter build */

static void ch_grow(Chapter *c, size_t extra)
{
    if (c->len + extra + 1 > c->cap) {
        c->cap = (uint32_t)((c->len + extra + 1) * 2);
        c->text = xrealloc(c->text, c->cap);
        c->style = xrealloc(c->style, c->cap);
    }
}

typedef struct {
    Chapter *c;
    uint32_t block_start;
    int kind;
    int pending_space;
    int next_noindent;
} Builder;

static void b_char(Builder *b, uint32_t cp, uint8_t style)
{
    Chapter *c = b->c;
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
        b->pending_space = c->len > b->block_start;
        return;
    }
    if (cp < 0x20 || cp == 0xFEFF || cp == 0x200B)
        return;
    ch_grow(c, 8);
    if (b->pending_space) {
        c->text[c->len] = ' ';
        c->style[c->len] = style;
        c->len++;
        b->pending_space = 0;
    }
    int n = utf8_encode(cp, c->text + c->len);
    memset(c->style + c->len, style, n);
    c->len += n;
}

static int is_scene_break(const char *s, size_t n)
{
    if (n == 0 || n > 16)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!strchr("*#~-_ .\xc2\xa7\xe2\x80\xa2\x81\x82\x83", s[i]))
            return 0;
    return 1;
}

static void b_end_block(Builder *b)
{
    Chapter *c = b->c;
    b->pending_space = 0;
    if (c->len == b->block_start)
        return;
    int kind = b->kind;
    if (kind == BLK_PARA && is_scene_break(c->text + b->block_start, c->len - b->block_start))
        kind = BLK_CENTER;
    if (kind == BLK_PARA && b->next_noindent)
        kind = BLK_PARA_NOINDENT;
    if (c->nblocks == c->capblocks) {
        c->capblocks = c->capblocks ? c->capblocks * 2 : 64;
        c->blocks = xrealloc(c->blocks, c->capblocks * sizeof(Block));
    }
    c->blocks[c->nblocks++] = (Block){ b->block_start, c->len, (uint8_t)kind };
    b->next_noindent = (kind == BLK_HEADING1 || kind == BLK_HEADING2 || kind == BLK_CENTER);
    b->block_start = c->len;
}

static void b_separator(Builder *b)
{
    b_end_block(b);
    Chapter *c = b->c;
    if (c->nblocks == 0 || c->blocks[c->nblocks - 1].kind == BLK_SEPARATOR)
        return;
    if (c->nblocks == c->capblocks) {
        c->capblocks *= 2;
        c->blocks = xrealloc(c->blocks, c->capblocks * sizeof(Block));
    }
    c->blocks[c->nblocks++] = (Block){ c->len, c->len, BLK_SEPARATOR };
    b->next_noindent = 1;
}

static int is_block_tag(const XmlTok *t)
{
    static const char *tags[] = { "p", "div", "li", "blockquote", "section", "article", "header", "footer",
                                  "figure", "figcaption", "dt", "dd", "tr", "table", "ul", "ol", "pre",
                                  "aside", "body", "center", "address", "h1", "h2", "h3", "h4", "h5", "h6" };
    for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++)
        if (name_is(t, tags[i]))
            return 1;
    return 0;
}

static int wants_center(const XmlTok *t)
{
    char v[256];
    if (name_is(t, "center"))
        return 1;
    if (xml_attr(t, "align", v, sizeof v) && strcasecmp(v, "center") == 0)
        return 1;
    if (xml_attr(t, "style", v, sizeof v)) {
        char *a = strstr(v, "text-align");
        if (a && strstr(a, "center"))
            return 1;
    }
    if (xml_attr(t, "class", v, sizeof v)) {
        for (char *p = v; *p; p++)
            *p = (char)tolower((unsigned char)*p);
        if (strstr(v, "center") || strstr(v, "centr"))
            return 1;
    }
    return 0;
}

static void parse_xhtml(Chapter *c, const char *data, size_t len)
{
    Builder b = { .c = c, .kind = BLK_PARA, .next_noindent = 1 };
    Xml x = { data, data + len };
    XmlTok t;
    int skip = 0, italic = 0, bold = 0, heading = 0, center = 0, in_body = 0;
    while (xml_next(&x, &t) != XT_END) {
        if (t.type == XT_OPEN || t.type == XT_CLOSE) {
            int open = t.type == XT_OPEN;
            if (name_is(&t, "body"))
                in_body = open;
            if (name_is(&t, "head") || name_is(&t, "script") || name_is(&t, "style") || name_is(&t, "title")) {
                if (!t.self_close)
                    skip += open ? 1 : -1;
                if (skip < 0)
                    skip = 0;
                continue;
            }
            if (t.self_close && !open)
                continue;
            if (name_is(&t, "em") || name_is(&t, "i") || name_is(&t, "cite") || name_is(&t, "var") ||
                name_is(&t, "dfn")) {
                if (!t.self_close)
                    italic += open ? 1 : (italic > 0 ? -1 : 0);
            } else if (name_is(&t, "b") || name_is(&t, "strong")) {
                if (!t.self_close)
                    bold += open ? 1 : (bold > 0 ? -1 : 0);
            } else if (name_is(&t, "br")) {
                b_end_block(&b);
                b.next_noindent = 1;
            } else if (name_is(&t, "hr")) {
                b_separator(&b);
            } else if (t.len == 2 && (t.s[0] == 'h' || t.s[0] == 'H') && t.s[1] >= '1' && t.s[1] <= '6') {
                b_end_block(&b);
                if (open && !t.self_close) {
                    heading = t.s[1] <= '2' ? BLK_HEADING1 : BLK_HEADING2;
                    b.kind = heading;
                } else {
                    heading = 0;
                    b.kind = center ? BLK_CENTER : BLK_PARA;
                }
            } else if (is_block_tag(&t)) {
                b_end_block(&b);
                if (open && !t.self_close && wants_center(&t))
                    center++;
                else if (!open && center > 0 && (name_is(&t, "center") || name_is(&t, "p") || name_is(&t, "div")))
                    center--; /* approximate: closes the innermost centred block */
                b.kind = heading ? heading : (center ? BLK_CENTER : BLK_PARA);
            }
            continue;
        }
        /* text */
        if (skip || !in_body)
            continue;
        uint8_t style = (uint8_t)((italic ? ST_ITALIC : 0) | ((bold || heading) ? ST_BOLD : 0));
        const char *s = t.s, *end = t.s + t.len;
        while (s < end) {
            uint32_t cp;
            if (*s == '&') {
                size_t l = decode_entity(s, end, &cp);
                if (l) {
                    b_char(&b, cp == 0xA0 ? 0xA0 : cp, style);
                    s += l;
                    continue;
                }
            }
            s += utf8_decode((const unsigned char *)s, end - s, &cp);
            b_char(&b, cp, style);
        }
    }
    b_end_block(&b);
}

static void parse_txt(Chapter *c, const char *s, size_t len, int wrapped)
{
    Builder b = { .c = c, .kind = BLK_PARA, .next_noindent = 1 };
    size_t i = 0;
    while (i < len) {
        if (s[i] == '\r') {
            i++;
            continue;
        }
        if (s[i] == '\n') {
            int blank = i + 1 < len && (s[i + 1] == '\n' || s[i + 1] == '\r');
            if (!wrapped || blank)
                b_end_block(&b);
            else
                b_char(&b, ' ', 0);
            i++;
            continue;
        }
        uint32_t cp;
        i += utf8_decode((const unsigned char *)s + i, len - i, &cp);
        b_char(&b, cp, 0);
    }
    b_end_block(&b);
}

Chapter *book_chapter(Book *b, int spine)
{
    if (spine < 0 || spine >= b->nspine)
        return NULL;
    Chapter *c = xcalloc(1, sizeof *c);
    double t0 = now_ms();
    if (b->is_txt) {
        TxtData *t = b->zip;
        parse_txt(c, t->data + t->starts[spine], t->starts[spine + 1] - t->starts[spine], t->wrapped);
    } else {
        size_t len;
        char *data = zip_read(b->zip, b->spine_href[spine], &len);
        if (!data) {
            log_msg("cannot read %s", b->spine_href[spine]);
            free(c);
            return NULL;
        }
        parse_xhtml(c, data, len);
        free(data);
    }
    log_msg("chapter %d: %u bytes, %d blocks, %.0f ms", spine, c->len, c->nblocks, now_ms() - t0);
    return c;
}

void chapter_free(Chapter *c)
{
    if (!c)
        return;
    free(c->text);
    free(c->style);
    free(c->blocks);
    free(c);
}
