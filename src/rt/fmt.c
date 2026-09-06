/* Minimal formatter: enough printf for logs and the terminal app, no C library required.
 *
 * Portable C (compiles for the SH-3 target and for the host test suite).
 */
#include "rt/rt.h"

typedef struct sink {
    unsigned short *w;   /* 16-bit output, or NULL */
    char *n;             /* 8-bit output, or NULL */
    unsigned int cap;
    unsigned int len;    /* characters produced so far (may exceed cap) */
} sink;

static void put(sink *s, unsigned int ch)
{
    if (s->len + 1 < s->cap) {
        if (s->w != NULL) {
            s->w[s->len] = (unsigned short)ch;
        } else {
            s->n[s->len] = (char)(ch < 0x80 ? ch : '?');
        }
    }
    s->len++;
}

static void finish(sink *s)
{
    if (s->cap == 0) {
        return;
    }
    unsigned int end = s->len < s->cap ? s->len : s->cap - 1;
    if (s->w != NULL) {
        s->w[end] = 0;
    } else {
        s->n[end] = 0;
    }
}

static void pad(sink *s, int count, unsigned int ch)
{
    while (count-- > 0) {
        put(s, ch);
    }
}

/* Render an unsigned value in the given base into tmp (reversed), return digit count. */
static int digits(unsigned int v, unsigned int base, int upper, char *tmp)
{
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do {
        tmp[n++] = set[v % base];
        v /= base;
    } while (v != 0);
    return n;
}

static void emit_number(sink *s, unsigned int v, unsigned int base, int upper, int negative,
                        int width, int left, int zero)
{
    char tmp[12];
    int n = digits(v, base, upper, tmp);
    int total = n + (negative ? 1 : 0);
    int fill = width > total ? width - total : 0;
    if (!left && !zero) {
        pad(s, fill, ' ');
    }
    if (negative) {
        put(s, '-');
    }
    if (!left && zero) {
        pad(s, fill, '0');
    }
    while (n > 0) {
        put(s, (unsigned int)(unsigned char)tmp[--n]);
    }
    if (left) {
        pad(s, fill, ' ');
    }
}

static void emit_wstr(sink *s, const unsigned short *str, int width, int left)
{
    static const unsigned short none[] = { '(', 'n', 'u', 'l', 'l', ')', 0 };
    int n;
    int fill;
    if (str == NULL) {
        str = none;
    }
    n = (int)rt_wcslen(str);
    fill = width > n ? width - n : 0;
    if (!left) {
        pad(s, fill, ' ');
    }
    while (*str != 0) {
        put(s, *str++);
    }
    if (left) {
        pad(s, fill, ' ');
    }
}

static void emit_str(sink *s, const char *str, int width, int left)
{
    int n;
    int fill;
    if (str == NULL) {
        str = "(null)";
    }
    n = (int)rt_strlen(str);
    fill = width > n ? width - n : 0;
    if (!left) {
        pad(s, fill, ' ');
    }
    while (*str != 0) {
        put(s, (unsigned int)(unsigned char)*str++);
    }
    if (left) {
        pad(s, fill, ' ');
    }
}

static int format(sink *s, const unsigned short *fmt, va_list ap)
{
    while (*fmt != 0) {
        int left = 0;
        int zero = 0;
        int width = 0;
        unsigned int c = *fmt++;
        if (c != '%') {
            put(s, c);
            continue;
        }
        for (;;) {
            if (*fmt == '-') {
                left = 1;
                fmt++;
            } else if (*fmt == '0') {
                zero = 1;
                fmt++;
            } else {
                break;
            }
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (int)(*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') {   /* accepted and ignored: all integers are 32-bit here */
            fmt++;
        }
        c = *fmt;
        if (c == 0) {
            break;
        }
        fmt++;
        switch (c) {
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            unsigned int mag = v < 0 ? (unsigned int)(-(v + 1)) + 1u : (unsigned int)v;
            emit_number(s, mag, 10, 0, v < 0, width, left, zero);
            break;
        }
        case 'u':
            emit_number(s, va_arg(ap, unsigned int), 10, 0, 0, width, left, zero);
            break;
        case 'x':
            emit_number(s, va_arg(ap, unsigned int), 16, 0, 0, width, left, zero);
            break;
        case 'X':
            emit_number(s, va_arg(ap, unsigned int), 16, 1, 0, width, left, zero);
            break;
        case 'p':
            put(s, '0');
            put(s, 'x');
            emit_number(s, (unsigned int)(unsigned long)va_arg(ap, void *), 16, 0, 0, 8, 0, 1);
            break;
        case 'c':
            put(s, (unsigned int)va_arg(ap, int) & 0xFFFFu);
            break;
        case 's':
            emit_wstr(s, va_arg(ap, const unsigned short *), width, left);
            break;
        case 'S':
            emit_str(s, va_arg(ap, const char *), width, left);
            break;
        case '%':
            put(s, '%');
            break;
        default:
            put(s, '%');
            put(s, c);
            break;
        }
    }
    finish(s);
    return (int)s->len;
}

int rt_vfmt(unsigned short *buf, unsigned int cap, const unsigned short *fmt, va_list ap)
{
    sink s = { buf, 0, cap, 0 };
    return format(&s, fmt, ap);
}

int rt_fmt(unsigned short *buf, unsigned int cap, const unsigned short *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = rt_vfmt(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int rt_vfmt8(char *buf, unsigned int cap, const unsigned short *fmt, va_list ap)
{
    sink s = { 0, buf, cap, 0 };
    return format(&s, fmt, ap);
}

int rt_fmt8(char *buf, unsigned int cap, const unsigned short *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = rt_vfmt8(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
