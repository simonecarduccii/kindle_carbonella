/*
 * Carbonella - the library: which books are on the Kindle and where we stopped reading.
 *
 * Books: every .epub and .txt under the books folder (default /mnt/us/documents, the folder you see
 * when the Kindle is plugged into a computer), up to 3 sub-folders deep.
 * Data (in /mnt/us/carbonella/dati, plain text so they can be read and backed up):
 *   libreria.tsv   title/author/text size cache, so we do not reopen every EPUB at each start
 *   posizioni.tsv  last position, percentage and estimated pages of every book
 *   letture.tsv    reading sessions (start, end, pages): the base for the "Obiettivi" screen
 */
#include "carbonella.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

static void add_entry(Library *lib, const char *path, const struct stat *st)
{
    if (lib->n == lib->cap) {
        lib->cap = lib->cap ? lib->cap * 2 : 32;
        lib->e = xrealloc(lib->e, lib->cap * sizeof(LibEntry));
    }
    LibEntry *e = &lib->e[lib->n++];
    memset(e, 0, sizeof *e);
    e->path = xstrdup(path);
    e->size = st->st_size;
    e->mtime = st->st_mtime;
    e->percent = -1;
}

static int is_book(const char *name)
{
    size_t n = strlen(name);
    return (n > 5 && strcasecmp(name + n - 5, ".epub") == 0) || (n > 4 && strcasecmp(name + n - 4, ".txt") == 0);
}

static void scan_dir(Library *lib, const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            size_t n = strlen(de->d_name);
            if (depth < 3 && !(n > 4 && strcmp(de->d_name + n - 4, ".sdr") == 0))
                scan_dir(lib, path, depth + 1);
        } else if (S_ISREG(st.st_mode) && is_book(de->d_name)) {
            add_entry(lib, path, &st);
        }
    }
    closedir(d);
}

LibEntry *library_find(Library *lib, const char *path)
{
    for (int i = 0; i < lib->n; i++)
        if (strcmp(lib->e[i].path, path) == 0)
            return &lib->e[i];
    return NULL;
}

/* Splits a TSV line in place. Returns the number of fields. */
static int split_tsv(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    for (char *p = line; *p && n < max; p++)
        if (*p == '\t') {
            *p = 0;
            f[n++] = p + 1;
        }
    return n;
}

static void sanitize(char *s)
{
    for (; *s; s++)
        if (*s == '\t' || *s == '\n' || *s == '\r')
            *s = ' ';
}

static int cmp_entries(const void *a, const void *b)
{
    const LibEntry *x = a, *y = b;
    if (x->last_read != y->last_read)
        return x->last_read > y->last_read ? -1 : 1;
    return strcasecmp(x->title ? x->title : "", y->title ? y->title : "");
}


/* v1.0.4: authors always "Nome Cognome" (Simone). EPUBs write them in many ways: "Calvino, Italo",
 * "Camus Albert" (and an opf:file-as just as wrong). In order:
 *  1. "Cognome, Nome" -> "Nome Cognome";
 *  2. the file is named "Nome Cognome - Titolo.epub" with the same words: the file's order wins;
 *  3. the first word is not a known first name and the last one is: the last goes first. */
static const char *const GIVEN[] = {
    "adriano", "agata", "alba", "alberto", "albert", "alda", "alessandro", "alessandra", "alice", "amos",
    "andrea", "anna", "antonio", "antonia", "arthur", "beppe", "bruno", "carlo", "carla", "cesare",
    "charles", "chiara", "clarice", "cormac", "cristina", "dacia", "daniele", "daniel", "dante", "david",
    "dino", "domenico", "donna", "edgar", "elena", "elio", "elsa", "emilio", "emily", "enrico", "ernest",
    "erri", "eugenio", "fabio", "federico", "fernando", "fëdor", "fedor", "fiodor", "francesco", "franz",
    "friedrich", "gabriel", "gabriele", "george", "giacomo", "gianni", "gianrico", "giorgio", "giovanni",
    "giuseppe", "giulio", "goliarda", "grazia", "gustave", "haruki", "henry", "herman", "hermann", "honoré",
    "ian", "ignazio", "isabel", "italo", "jack", "jane", "james", "jean", "jhumpa", "joan", "johann",
    "john", "jorge", "josé", "joseph", "julio", "karl", "kazuo", "lev", "leo", "leonardo", "lewis", "luigi",
    "luis", "luca", "marcel", "marco", "margaret", "margherita", "maria", "mario", "mark", "michael",
    "michele", "milan", "natalia", "niccolò", "nicola", "oscar", "pablo", "paolo", "patrick", "paul",
    "philip", "pier", "piero", "primo", "raymond", "roberto", "roberta", "salvatore", "sandro", "simone",
    "stefano", "stephen", "susanna", "sylvia", "thomas", "tiziano", "toni", "truman", "umberto", "valeria",
    "vasilij", "vasco", "vincenzo", "virginia", "vitaliano", "walter", "william", "wisława", "yasunari",
    "zadie", NULL,
};

static int is_given(const char *w, size_t n)
{
    char low[64];
    if (n == 0 || n >= sizeof low)
        return 0;
    for (size_t i = 0; i < n; i++)
        low[i] = (char)((unsigned char)w[i] < 128 ? tolower((unsigned char)w[i]) : w[i]);
    low[n] = 0;
    for (int i = 0; GIVEN[i]; i++)
        if (!strcmp(GIVEN[i], low))
            return 1;
    return 0;
}

/* same words, any order, ignoring case (ASCII) */
static int same_words(const char *a, const char *b)
{
    char wa[16][64], wb[16][64];
    int na = 0, nb = 0;
    for (int pass = 0; pass < 2; pass++) {
        const char *s = pass ? b : a;
        char (*w)[64] = pass ? wb : wa;
        int *n = pass ? &nb : &na;
        while (*s) {
            while (*s == ' ')
                s++;
            size_t l = 0;
            while (s[l] && s[l] != ' ')
                l++;
            if (l == 0)
                break;
            if (*n == 16 || l >= 64)
                return 0;
            for (size_t i = 0; i < l; i++)
                w[*n][i] = (char)tolower((unsigned char)s[i]);
            w[*n][l] = 0;
            (*n)++;
            s += l;
        }
    }
    if (na != nb || na < 2)
        return 0;
    int used[16] = { 0 };
    for (int i = 0; i < na; i++) {
        int found = 0;
        for (int j = 0; j < nb && !found; j++)
            if (!used[j] && !strcmp(wa[i], wb[j]))
                used[j] = found = 1;
        if (!found)
            return 0;
    }
    return 1;
}

static char *normalize_author(char *au, const char *path)
{
    if (!au || !*au || strchr(au, ';') || strchr(au, '&'))
        return au;
    char *comma = strchr(au, ',');
    if (comma && !strchr(comma + 1, ',')) { /* "Cognome, Nome" */
        char *given = comma + 1;
        while (*given == ' ')
            given++;
        if (*given) {
            size_t n = strlen(given) + (size_t)(comma - au) + 2;
            char *r = xmalloc(n);
            snprintf(r, n, "%s %.*s", given, (int)(comma - au), au);
            free(au);
            return r;
        }
    }
    /* the file name "Nome Cognome - Titolo.epub" */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dash = strstr(base, " - ");
    if (dash && dash - base < 200) {
        char fa[200];
        snprintf(fa, sizeof fa, "%.*s", (int)(dash - base), base);
        if (strcmp(fa, au) && same_words(fa, au)) {
            free(au);
            return xstrdup(fa);
        }
        if (!strcmp(fa, au))
            return au;
    }
    /* "Camus Albert": two or more words, a first name at the end and not at the start */
    char *sp1 = strchr(au, ' '), *spl = strrchr(au, ' ');
    if (sp1 && !is_given(au, (size_t)(sp1 - au)) && is_given(spl + 1, strlen(spl + 1))) {
        size_t n = strlen(au) + 2;
        char *r = xmalloc(n);
        snprintf(r, n, "%s %.*s", spl + 1, (int)(spl - au), au);
        free(au);
        return r;
    }
    return au;
}

void library_scan(Library *lib, const char *books_dir, const char *data_dir)
{
    double t0 = now_ms();
    library_free(lib);
    scan_dir(lib, books_dir, 0);

    /* titles from the cache, or from the files */
    char path[1024];
    snprintf(path, sizeof path, "%s/libreria.tsv", data_dir);
    char *cache = read_file(path, NULL);
    int from_cache = 0, opened = 0;
    if (cache) {
        char *save = NULL;
        for (char *line = strtok_r(cache, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *f[7];
            if (split_tsv(line, f, 7) < 7) /* lines before v0.6 had no publisher: read the book again */
                continue;
            LibEntry *e = library_find(lib, f[0]);
            if (e && !e->title && atoll(f[1]) == e->size && atoll(f[2]) == e->mtime) {
                e->title = xstrdup(f[3]);
                e->author = xstrdup(f[4]);
                e->text_bytes = atoll(f[5]);
                e->publisher = xstrdup(f[6]);
                from_cache++;
            }
        }
        free(cache);
    }
    for (int i = 0; i < lib->n; i++) {
        LibEntry *e = &lib->e[i];
        if (e->title)
            continue;
        free(e->publisher);
        e->publisher = NULL;
        if (book_peek_meta(e->path, &e->title, &e->author, &e->publisher, &e->text_bytes) != 0) {
            const char *s = strrchr(e->path, '/');
            e->title = xstrdup(s ? s + 1 : e->path);
            e->author = xstrdup("(file non leggibile)");
        }
        if (!e->publisher)
            e->publisher = xstrdup("");
        sanitize(e->title);
        sanitize(e->author);
        sanitize(e->publisher);
        opened++;
    }
    /* titles and authors always clean, also the ones from an older cache */
    int changed = 0;
    for (int i = 0; i < lib->n; i++) {
        LibEntry *e = &lib->e[i];
        size_t tl = strlen(e->title);
        char *old = e->author;
        book_clean_title(e->title);
        e->author = normalize_author(e->author, e->path);
        changed |= strlen(e->title) != tl || e->author != old;
    }
    if (opened || changed) { /* the cache also gets the clean names */
        size_t cap = 4096, n = 0;
        char *buf = xmalloc(cap);
        for (int i = 0; i < lib->n; i++) {
            LibEntry *e = &lib->e[i];
            size_t need = strlen(e->path) + strlen(e->title) + strlen(e->author) + strlen(e->publisher) + 64;
            if (n + need > cap) {
                cap = (n + need) * 2;
                buf = xrealloc(buf, cap);
            }
            n += snprintf(buf + n, cap - n, "%s\t%lld\t%lld\t%s\t%s\t%lld\t%s\n", e->path, e->size, e->mtime,
                          e->title, e->author, e->text_bytes, e->publisher);
        }
        write_file_atomic(path, buf, n);
        free(buf);
    }

    /* saved positions */
    snprintf(path, sizeof path, "%s/posizioni.tsv", data_dir);
    char *pos = read_file(path, NULL);
    if (pos) {
        char *save = NULL;
        for (char *line = strtok_r(pos, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *f[6];
            int nf = split_tsv(line, f, 6);
            if (nf < 5)
                continue;
            LibEntry *e = library_find(lib, f[0]);
            if (!e)
                continue;
            if (nf >= 6)
                e->pages = atoi(f[5]);
            e->spine = atoi(f[1]);
            e->pos = (uint32_t)strtoul(f[2], NULL, 10);
            e->percent = atoi(f[3]);
            e->last_read = atoll(f[4]);
        }
        free(pos);
    }
    qsort(lib->e, lib->n, sizeof(LibEntry), cmp_entries);
    log_msg("library: %d books (%d from cache, %d opened) in %.0f ms", lib->n, from_cache, opened, now_ms() - t0);
}

void library_save_positions(const Library *lib, const char *data_dir)
{
    size_t cap = 4096, n = 0;
    char *buf = xmalloc(cap);
    for (int i = 0; i < lib->n; i++) {
        const LibEntry *e = &lib->e[i];
        if (e->percent < 0)
            continue;
        size_t need = strlen(e->path) + 96;
        if (n + need > cap) {
            cap = (n + need) * 2;
            buf = xrealloc(buf, cap);
        }
        n += snprintf(buf + n, cap - n, "%s\t%d\t%u\t%d\t%lld\t%d\n", e->path, e->spine, e->pos, e->percent,
                      e->last_read, e->pages);
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/posizioni.tsv", data_dir);
    write_file_atomic(path, buf, n);
    free(buf);
}

void reading_log(const char *data_dir, const char *path, long long start, long long end, int pages)
{
    if (end - start < 10 && pages == 0)
        return;
    char file[1024];
    snprintf(file, sizeof file, "%s/letture.tsv", data_dir);
    FILE *f = fopen(file, "a");
    if (!f)
        return;
    fprintf(f, "%lld\t%lld\t%d\t%s\n", start, end, pages, path);
    fclose(f);
}

void library_free(Library *lib)
{
    for (int i = 0; i < lib->n; i++) {
        free(lib->e[i].path);
        free(lib->e[i].title);
        free(lib->e[i].author);
        free(lib->e[i].publisher);
    }
    free(lib->e);
    memset(lib, 0, sizeof *lib);
}
