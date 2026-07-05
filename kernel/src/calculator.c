#include <calculator.h>
#include <stdint.h>
#include <window.h>
#include <fb.h>
#include <font.h>
#include <serial.h>
#include <taskbar.h>

#define CAL_EXPR_MAX 24

static int cal_win = -1;
static char cal_expr[CAL_EXPR_MAX] = "";
static int cal_result_shown = 0;
static char cal_error[16] = "";

static void cal_append_char(char c) {
    if (cal_result_shown) {
        cal_expr[0] = '\0';
        cal_result_shown = 0;
    }
    cal_error[0] = '\0';
    int len = 0;
    while (cal_expr[len]) len++;
    if (len < CAL_EXPR_MAX - 1) {
        cal_expr[len] = c;
        cal_expr[len + 1] = '\0';
    }
}

static int is_op(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/';
}

static void cal_press_op(char op) {
    if (cal_result_shown) {
        cal_result_shown = 0; /* chain off the just-shown result */
    }
    int len = 0;
    while (cal_expr[len]) len++;
    if (len == 0) {
        return; /* can't start an expression with an operator (no negative-number entry, per scope) */
    }
    if (is_op(cal_expr[len - 1])) {
        cal_expr[len - 1] = op; /* replace a just-pressed operator instead of stacking two */
        return;
    }
    cal_append_char(op);
}

static int64_t parse_int(const char *s, int len) {
    int neg = 0;
    int i = 0;
    if (len > 0 && s[0] == '-') {
        neg = 1;
        i = 1;
    }
    int64_t v = 0;
    for (; i < len; i++) {
        v = v * 10 + (s[i] - '0');
    }
    return neg ? -v : v;
}

/* Detects signed 64-bit overflow for the four ops via a division-based
 * check performed BEFORE the operation, matching the existing "Error: div
 * by 0" pattern of catching a bad op ahead of computing a wrong result. */
static int op_would_overflow(char op, int64_t left, int64_t right) {
    switch (op) {
        case '+':
            if (right > 0 && left > INT64_MAX - right) return 1;
            if (right < 0 && left < INT64_MIN - right) return 1;
            return 0;
        case '-':
            if (right < 0 && left > INT64_MAX + right) return 1;
            if (right > 0 && left < INT64_MIN + right) return 1;
            return 0;
        case '*':
            if (left == 0 || right == 0) return 0;
            if (left == -1 && right == INT64_MIN) return 1;
            if (right == -1 && left == INT64_MIN) return 1;
            int64_t product = left * right;
            return product / right != left;
        default:
            return 0;
    }
}

static void append_result_digits(int64_t v) {
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
    for (int i = 0; i < pos; i++) {
        cal_expr[i] = buf[pos - 1 - i];
    }
    cal_expr[pos] = '\0';
}

static void cal_press_equals(void) {
    int len = 0;
    while (cal_expr[len]) len++;
    int op_pos = -1;
    char op = 0;
    /* Skip a leading '-' when scanning for the operator: it's the sign of
     * the left operand (e.g. a chained "-2+4"), not the operator itself. */
    int scan_start = (len > 0 && cal_expr[0] == '-') ? 1 : 0;
    for (int i = scan_start; i < len; i++) {
        if (is_op(cal_expr[i])) {
            op_pos = i;
            op = cal_expr[i];
            break;
        }
    }
    if (op_pos <= 0 || op_pos == len - 1) {
        return; /* no complete "<left><op><right>" to evaluate yet */
    }
    int64_t left = parse_int(cal_expr, op_pos);
    int64_t right = parse_int(cal_expr + op_pos + 1, len - op_pos - 1);
    int64_t result = 0;
    if (op == '/' && right == 0) {
        int i = 0;
        const char *msg = "Error: div by 0";
        while (msg[i] && i < (int)sizeof(cal_error) - 1) {
            cal_error[i] = msg[i];
            i++;
        }
        cal_error[i] = '\0';
        cal_expr[0] = '\0';
        cal_result_shown = 1;
        return;
    }
    if (op_would_overflow(op, left, right)) {
        int i = 0;
        const char *msg = "Error: overflow";
        while (msg[i] && i < (int)sizeof(cal_error) - 1) {
            cal_error[i] = msg[i];
            i++;
        }
        cal_error[i] = '\0';
        cal_expr[0] = '\0';
        cal_result_shown = 1;
        return;
    }
    switch (op) {
        case '+': result = left + right; break;
        case '-': result = left - right; break;
        case '*': result = left * right; break;
        case '/': result = left / right; break;
    }
    append_result_digits(result);
    cal_result_shown = 1;

    serial_write_string("Calculator: ");
    serial_write_string(cal_expr); /* now holds just the result */
    serial_write_string("\n");
}

static void cal_press_clear(void) {
    cal_expr[0] = '\0';
    cal_error[0] = '\0';
    cal_result_shown = 0;
}

/* window_add_button's callback type takes no arguments, so each key needs
 * its own tiny dedicated function - mechanical, but the alternative (a
 * hand-rolled hit-test in a custom_click callback) would mean re-testing
 * click geometry the button/window system already handles correctly. */
static void cal_0(void) { cal_append_char('0'); }
static void cal_1(void) { cal_append_char('1'); }
static void cal_2(void) { cal_append_char('2'); }
static void cal_3(void) { cal_append_char('3'); }
static void cal_4(void) { cal_append_char('4'); }
static void cal_5(void) { cal_append_char('5'); }
static void cal_6(void) { cal_append_char('6'); }
static void cal_7(void) { cal_append_char('7'); }
static void cal_8(void) { cal_append_char('8'); }
static void cal_9(void) { cal_append_char('9'); }
static void cal_plus(void) { cal_press_op('+'); }
static void cal_minus(void) { cal_press_op('-'); }
static void cal_mul(void) { cal_press_op('*'); }
static void cal_div(void) { cal_press_op('/'); }
static void cal_eq(void) { cal_press_equals(); }
static void cal_clear(void) { cal_press_clear(); }

static void cal_render(struct window *win) {
    int64_t body_x = win->x;
    int64_t body_y = win->y + WINDOW_TITLEBAR_HEIGHT;
    fb_draw_rect(body_x + 4, body_y + 4, win->w - 8, 30, fb_make_color(15, 15, 20));
    const char *display = cal_error[0] ? cal_error : (cal_expr[0] ? cal_expr : "0");
    fb_draw_string_clipped(body_x + 8, body_y + 15, display,
                            fb_make_color(120, 255, 150), fb_make_color(15, 15, 20),
                            body_x + 4, body_y + 4, win->w - 8, 30);
}

/* Mirrors terminal_on_close(): without this, closing the Calculator via its
 * titlebar X would leave cal_win pointing at a freed slot, and the next
 * calculator_open() call's window_focus() would silently no-op instead of
 * creating a fresh window (kernel/src/window.c:194's in_use guard) - the
 * desktop's Calculator icon would appear permanently dead after one close. */
static void cal_on_close(struct window *win) {
    (void)win;
    cal_win = -1;
}

void calculator_open(void) {
    if (cal_win != -1) {
        window_focus(cal_win);
        return;
    }
    cal_win = window_create(560, 90, 220, 300, fb_make_color(150, 110, 170),
                             fb_make_color(40, 30, 45), "Calculator");
    if (cal_win == -1) {
        serial_write_string("Calculator: window_create() failed (MAX_WINDOWS exhausted)\n");
        taskbar_flash_message("Could not open Calculator - too many windows open");
        return;
    }
    window_set_render_callback(cal_win, cal_render);
    window_set_close_callback(cal_win, cal_on_close);
    cal_press_clear();

    /* 4x4 grid below the display strip (which occupies body y=[4,38)). */
    int64_t gx = 8, gy = 44;
    uint64_t bw = 46, bh = 46, gap = 4;
    uint32_t digit_color = fb_make_color(70, 70, 90);
    uint32_t op_color = fb_make_color(120, 90, 150);
    uint32_t eq_color = fb_make_color(90, 150, 110);
    uint32_t clr_color = fb_make_color(170, 90, 90);

    window_add_button(cal_win, gx + 0 * (int64_t)(bw + gap), gy + 0 * (int64_t)(bh + gap), bw, bh, digit_color, "7", cal_7);
    window_add_button(cal_win, gx + 1 * (int64_t)(bw + gap), gy + 0 * (int64_t)(bh + gap), bw, bh, digit_color, "8", cal_8);
    window_add_button(cal_win, gx + 2 * (int64_t)(bw + gap), gy + 0 * (int64_t)(bh + gap), bw, bh, digit_color, "9", cal_9);
    window_add_button(cal_win, gx + 3 * (int64_t)(bw + gap), gy + 0 * (int64_t)(bh + gap), bw, bh, op_color, "/", cal_div);

    window_add_button(cal_win, gx + 0 * (int64_t)(bw + gap), gy + 1 * (int64_t)(bh + gap), bw, bh, digit_color, "4", cal_4);
    window_add_button(cal_win, gx + 1 * (int64_t)(bw + gap), gy + 1 * (int64_t)(bh + gap), bw, bh, digit_color, "5", cal_5);
    window_add_button(cal_win, gx + 2 * (int64_t)(bw + gap), gy + 1 * (int64_t)(bh + gap), bw, bh, digit_color, "6", cal_6);
    window_add_button(cal_win, gx + 3 * (int64_t)(bw + gap), gy + 1 * (int64_t)(bh + gap), bw, bh, op_color, "x", cal_mul);

    window_add_button(cal_win, gx + 0 * (int64_t)(bw + gap), gy + 2 * (int64_t)(bh + gap), bw, bh, digit_color, "1", cal_1);
    window_add_button(cal_win, gx + 1 * (int64_t)(bw + gap), gy + 2 * (int64_t)(bh + gap), bw, bh, digit_color, "2", cal_2);
    window_add_button(cal_win, gx + 2 * (int64_t)(bw + gap), gy + 2 * (int64_t)(bh + gap), bw, bh, digit_color, "3", cal_3);
    window_add_button(cal_win, gx + 3 * (int64_t)(bw + gap), gy + 2 * (int64_t)(bh + gap), bw, bh, op_color, "-", cal_minus);

    window_add_button(cal_win, gx + 0 * (int64_t)(bw + gap), gy + 3 * (int64_t)(bh + gap), bw, bh, clr_color, "C", cal_clear);
    window_add_button(cal_win, gx + 1 * (int64_t)(bw + gap), gy + 3 * (int64_t)(bh + gap), bw, bh, digit_color, "0", cal_0);
    window_add_button(cal_win, gx + 2 * (int64_t)(bw + gap), gy + 3 * (int64_t)(bh + gap), bw, bh, eq_color, "=", cal_eq);
    window_add_button(cal_win, gx + 3 * (int64_t)(bw + gap), gy + 3 * (int64_t)(bh + gap), bw, bh, op_color, "+", cal_plus);

    serial_write_string("Calculator: opened\n");
}

int calculator_is_open(void) {
    return cal_win != -1 && window_get(cal_win) != 0;
}

void calculator_close(void) {
    if (cal_win != -1) {
        window_close(cal_win); /* invokes cal_on_close(), which resets cal_win */
    }
}
