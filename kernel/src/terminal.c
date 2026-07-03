#include <terminal.h>
#include <window.h>
#include <fat32.h>
#include <process.h>
#include <fb.h>
#include <serial.h>
#include <taskbar.h>

static int term_win = -1;
/* Current directory as a fat_resolve_path()-compatible relative path;
 * "" means root. No leading/trailing slash, components joined by '/' -
 * same convention fat_resolve_path's own path-splitting expects. */
static char term_cwd[200] = "";

static void term_append(struct window *win, const char *s) {
    while (*s && win->textbox_length < TEXTBOX_BUFFER_SIZE - 1) {
        win->textbox_buffer[win->textbox_length++] = *s++;
    }
    win->textbox_buffer[win->textbox_length] = '\0';
}

static void term_print_prompt(struct window *win) {
    term_append(win, "\n/");
    term_append(win, term_cwd);
    term_append(win, "> ");
}

/* Splits the last (currently-being-typed) line out of the textbox buffer -
 * everything after the final '\n', or the whole buffer if there is none. */
static const char *term_current_line(struct window *win) {
    for (int i = win->textbox_length - 1; i >= 0; i--) {
        if (win->textbox_buffer[i] == '\n') {
            return &win->textbox_buffer[i + 1];
        }
    }
    return win->textbox_buffer;
}

static int str_eq(const char *a, const char *b) {
    int i = 0;
    for (; a[i] && b[i]; i++) {
        if (a[i] != b[i]) return 0;
    }
    return a[i] == b[i];
}

/* Splits "cmd arg" (the part after the prompt) into cmd/arg, trimming the
 * single space separator; arg is "" if there isn't one. */
static void split_command(const char *line, char *cmd, int cmd_max, char *arg, int arg_max) {
    int i = 0, j = 0;
    while (line[i] && line[i] != ' ' && j < cmd_max - 1) {
        cmd[j++] = line[i++];
    }
    cmd[j] = '\0';
    while (line[i] == ' ') i++;
    j = 0;
    while (line[i] && j < arg_max - 1) {
        arg[j++] = line[i++];
    }
    arg[j] = '\0';
}

static void run_ls(struct window *win) {
    struct fat_dirent d;
    if (!fat_resolve_path(term_cwd, &d) || !(d.attr & FAT32_ATTR_DIRECTORY)) {
        term_append(win, "\nls: current directory is gone?!");
        return;
    }
    struct fat_dirent entries[FAT32_MAX_DIRENTS];
    int count = fat_list_dir(d.first_cluster, entries, FAT32_MAX_DIRENTS);
    int shown = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].name[0] == '.' &&
            (entries[i].name[1] == '\0' || (entries[i].name[1] == '.' && entries[i].name[2] == '\0'))) {
            continue; /* hide "." / ".." like a normal shell's default ls */
        }
        term_append(win, "\n");
        if (entries[i].attr & FAT32_ATTR_DIRECTORY) {
            term_append(win, "[");
            term_append(win, entries[i].name);
            term_append(win, "]");
        } else {
            term_append(win, entries[i].name);
        }
        shown++;
    }
    if (shown == 0) {
        term_append(win, "\n(empty)");
    }
}

static void run_cd(struct window *win, const char *arg) {
    if (arg[0] == '\0') {
        term_cwd[0] = '\0'; /* bare "cd" -> back to root, like a shell's HOME */
        return;
    }
    if (str_eq(arg, "..")) {
        int len = 0;
        while (term_cwd[len]) len++;
        int last_slash = -1;
        for (int i = 0; i < len; i++) {
            if (term_cwd[i] == '/') last_slash = i;
        }
        if (last_slash >= 0) {
            term_cwd[last_slash] = '\0';
        } else {
            term_cwd[0] = '\0'; /* already one level below root */
        }
        return;
    }

    char candidate[200];
    int i = 0;
    if (term_cwd[0]) {
        while (term_cwd[i] && i < (int)sizeof(candidate) - 2) {
            candidate[i] = term_cwd[i];
            i++;
        }
        candidate[i++] = '/';
    }
    int j = 0;
    while (arg[j] && i < (int)sizeof(candidate) - 1) {
        candidate[i++] = arg[j++];
    }
    candidate[i] = '\0';

    struct fat_dirent d;
    if (!fat_resolve_path(candidate, &d)) {
        term_append(win, "\ncd: no such directory: ");
        term_append(win, arg);
        return;
    }
    if (!(d.attr & FAT32_ATTR_DIRECTORY)) {
        term_append(win, "\ncd: not a directory: ");
        term_append(win, arg);
        return;
    }
    int k = 0;
    while (candidate[k] && k < (int)sizeof(term_cwd) - 1) {
        term_cwd[k] = candidate[k];
        k++;
    }
    term_cwd[k] = '\0';
}

static void append_int(struct window *win, int64_t v) {
    char buf[24];
    int pos = 0;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    if (u == 0) {
        buf[pos++] = '0';
    }
    while (u > 0) {
        buf[pos++] = (char)('0' + (u % 10));
        u /= 10;
    }
    if (neg) buf[pos++] = '-';
    char out[25];
    for (int i = 0; i < pos; i++) out[i] = buf[pos - 1 - i];
    out[pos] = '\0';
    term_append(win, out);
}

/* Milestone 24.1's "run" - a genuinely user-mode launch, not a cosmetic
 * one: process_spawn() loads and maps a real ELF64 binary into its own
 * ring-3 address space, and scheduler_run_until_done() blocks (via the
 * real timer-preemptive scheduler, not a busy-poll) until it reaches
 * PROC_ZOMBIE, after which process_get() reports its actual exit code -
 * the same infrastructure Phase 20's multitasking tests exercise. `run`
 * only accepts bundled root-level paths (e.g. "HELLO.ELF"); it does not
 * resolve relative to the terminal's own `cd` state. */
static void run_run(struct window *win, const char *arg) {
    if (arg[0] == '\0') {
        term_append(win, "\nusage: run <NAME.ELF>");
        return;
    }
    int pid = process_spawn(arg);
    if (pid < 0) {
        term_append(win, "\nrun: could not spawn ");
        term_append(win, arg);
        return;
    }
    scheduler_run_until_done();
    struct process *p = process_get(pid);
    term_append(win, "\nprocess ");
    append_int(win, pid);
    term_append(win, " exited with code ");
    append_int(win, p ? p->exit_code : -1);
}

static void run_help(struct window *win) {
    term_append(win, "\ncommands: ls, cd <dir>, cd .., run <name.elf>, clear, help");
}

static int terminal_on_key(struct window *win, char c) {
    if (c != '\n') {
        return 0; /* let default typing (append/backspace) handle everything else */
    }

    char line[200];
    const char *raw = term_current_line(win);
    int i = 0;
    while (raw[i] && i < (int)sizeof(line) - 1) {
        line[i] = raw[i];
        i++;
    }
    line[i] = '\0';

    /* term_current_line() returns everything since the last '\n', which
     * includes the "/cwd> " prompt text itself (appended by
     * term_print_prompt() on the same logical line the user then types
     * into) - skip past exactly that many characters so split_command()
     * sees only what the user actually typed, not "/> ls" as if "/>" were
     * part of the command name. */
    char prompt[210];
    prompt[0] = '/';
    int pi = 1;
    for (int k = 0; term_cwd[k] && pi < (int)sizeof(prompt) - 3; k++) prompt[pi++] = term_cwd[k];
    prompt[pi++] = '>';
    prompt[pi++] = ' ';
    prompt[pi] = '\0';
    const char *typed = line;
    int plen = 0;
    while (prompt[plen] && prompt[plen] == line[plen]) plen++;
    if (prompt[plen] == '\0') {
        typed = line + plen;
    }

    char cmd[32], arg[160];
    split_command(typed, cmd, sizeof(cmd), arg, sizeof(arg));

    serial_write_string("Terminal: command \"");
    serial_write_string(typed);
    serial_write_string("\"\n");

    if (cmd[0] == '\0') {
        /* blank line - just re-prompt */
    } else if (str_eq(cmd, "ls")) {
        run_ls(win);
    } else if (str_eq(cmd, "cd")) {
        run_cd(win, arg);
    } else if (str_eq(cmd, "run")) {
        run_run(win, arg);
    } else if (str_eq(cmd, "help")) {
        run_help(win);
    } else if (str_eq(cmd, "clear")) {
        win->textbox_length = 0;
        win->textbox_buffer[0] = '\0';
        term_print_prompt(win);
        return 1;
    } else {
        term_append(win, "\nunknown command: ");
        term_append(win, cmd);
        term_append(win, " (try 'help')");
    }

    term_print_prompt(win);
    return 1; /* we handled the newline ourselves - don't also insert it literally */
}

/* Without this, closing the Terminal via its titlebar X button would leave
 * term_win pointing at a now-freed (in_use=0) slot; window_focus() silently
 * no-ops on an invalid/closed index (kernel/src/window.c:194), so the next
 * terminal_open() call would neither create a new window nor focus
 * anything - the desktop's Terminal icon would appear permanently dead
 * after the first close. Resetting term_win here (mirroring the same
 * pattern this phase also applies to the Calculator) makes reopen work
 * correctly every time. */
static void terminal_on_close(struct window *win) {
    (void)win;
    term_win = -1;
}

void terminal_open(void) {
    if (term_win != -1) {
        window_focus(term_win);
        return;
    }
    term_win = window_create(260, 90, 400, 260, fb_make_color(90, 90, 160),
                              fb_make_color(20, 20, 30), "Terminal");
    if (term_win == -1) {
        serial_write_string("Terminal: window_create() failed (MAX_WINDOWS exhausted)\n");
        taskbar_flash_message("Could not open Terminal - too many windows open");
        return;
    }
    window_enable_textbox(term_win);
    window_set_key_callback(term_win, terminal_on_key);
    window_set_close_callback(term_win, terminal_on_close);

    struct window *win = window_get(term_win);
    term_cwd[0] = '\0';
    win->textbox_length = 0;
    win->textbox_buffer[0] = '\0';
    term_append(win, "gOS Terminal - type 'help' for commands");
    term_print_prompt(win);

    serial_write_string("Terminal: opened\n");
}

int terminal_is_open(void) {
    return term_win != -1 && window_get(term_win) != 0;
}

void terminal_close(void) {
    if (term_win != -1) {
        window_close(term_win); /* invokes terminal_on_close(), which resets term_win */
    }
}
