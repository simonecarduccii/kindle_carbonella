/*
 * Carbonella - Kindle 4 keys.
 *
 * Linux key codes of the K4 (from KOReader's event_map_kindle4.lua, to be confirmed on our
 * device with "Prova dei tasti"):
 *   102 Home  103 Up  105 Left  106 Right  108 Down  194 centre of the 5-way
 *   139 Menu  158 Back  29 Keyboard
 *   104 left "next page"   193 left "previous page"
 *   191 right "next page"  109 right "previous page"
 *
 * We open every /dev/input/event* and "grab" it so no other program receives the keys
 * while Carbonella runs.
 */
#include "carbonella.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* The kernel's struct input_event, written out by hand: musl uses 64-bit time_t even on 32-bit
 * ARM, but the K4 kernel (2.6.31) sends 32-bit timestamps. 'long' has the kernel's size. */
struct k_input_event {
    long sec, usec;
    uint16_t type, code;
    int32_t value;
};

#define MAX_DEV 10
static int fds[MAX_DEV];
static int nfds;
static const char *script;

const char *key_name(Key k)
{
    static const char *names[] = { "nessuno", "su", "giu", "sinistra", "destra", "centro",
                                   "avanti", "indietro", "Home", "Menu", "Back", "Tastiera",
                                   "timeout", "standby", "risveglio", "fine" };
    return k <= K_QUIT ? names[k] : "?";
}

static Key map_code(int code)
{
    switch (code) {
    case 103: return K_UP;
    case 108: return K_DOWN;
    case 105: return K_LEFT;
    case 106: return K_RIGHT;
    case 194:
    case 28: return K_SELECT;
    case 104:
    case 191: return K_NEXT;
    case 109:
    case 193: return K_PREV;
    case 102: return K_HOME;
    case 139: return K_MENU;
    case 158:
    case 1: return K_BACK;
    case 29:
    case 190: return K_KEYBOARD;
    default: return K_NONE;
    }
}

int input_open(const char *sim_script)
{
    if (sim_script) {
        script = sim_script;
        return 0;
    }
    for (int i = 0; i < MAX_DEV; i++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        char name[64] = "?";
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        int grabbed = ioctl(fd, EVIOCGRAB, 1) == 0;
        log_msg("input %s \"%s\"%s", path, name, grabbed ? " (grabbed)" : "");
        fds[nfds++] = fd;
    }
    if (nfds == 0) {
        log_msg("no input devices");
        return -1;
    }
    return 0;
}

void input_close(void)
{
    for (int i = 0; i < nfds; i++) {
        ioctl(fds[i], EVIOCGRAB, 0);
        close(fds[i]);
    }
    nfds = 0;
}

static Key script_next(int *raw)
{
    while (*script == ',' || *script == ' ')
        script++;
    if (!*script)
        return K_QUIT;
    char c = *script++;
    *raw = c;
    switch (c) {
    case 'U': return K_UP;
    case 'D': return K_DOWN;
    case 'L': return K_LEFT;
    case 'R': return K_RIGHT;
    case 'S': return K_SELECT;
    case 'N': return K_NEXT;
    case 'P': return K_PREV;
    case 'H': return K_HOME;
    case 'M': return K_MENU;
    case 'B': return K_BACK;
    case 'K': return K_KEYBOARD;
    case 'W': return K_TIMEOUT;
    case 'Z': return K_SLEEP;
    case 'A': return K_WAKE;
    default: return K_NONE;
    }
}

Key input_wait(int timeout_ms, int *raw_code)
{
    int dummy;
    if (!raw_code)
        raw_code = &dummy;
    *raw_code = 0;
    if (script)
        return script_next(raw_code);

    double deadline = timeout_ms >= 0 ? now_ms() + timeout_ms : 0;
    for (;;) {
        if (g_quit)
            return K_QUIT;
        if (g_power == 1 || g_power == 2) {
            Key k = g_power == 1 ? K_SLEEP : K_WAKE;
            g_power = 0;
            return k;
        }
        struct pollfd p[MAX_DEV];
        for (int i = 0; i < nfds; i++)
            p[i] = (struct pollfd){ .fd = fds[i], .events = POLLIN };
        int wait = -1;
        if (timeout_ms >= 0) {
            wait = (int)(deadline - now_ms());
            if (wait <= 0)
                return K_TIMEOUT;
        }
        int r = poll(p, nfds, wait);
        if (r < 0) {
            if (errno == EINTR && !g_quit) /* a signal: maybe g_power */
                continue;
            return K_QUIT;
        }
        if (r == 0)
            return K_TIMEOUT;
        for (int i = 0; i < nfds; i++) {
            if (!(p[i].revents & POLLIN))
                continue;
            struct k_input_event ev;
            while (read(fds[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (ev.type != EV_KEY || ev.value == 0)
                    continue;
                Key k = map_code(ev.code);
                /* held keys repeat only for moving around lists */
                if (ev.value == 2 && !(k == K_UP || k == K_DOWN || k == K_LEFT || k == K_RIGHT))
                    continue;
                *raw_code = ev.code;
                if (k == K_NONE)
                    log_msg("unknown key code %d", ev.code);
                return k == K_NONE ? K_NONE : k;
            }
        }
    }
}
