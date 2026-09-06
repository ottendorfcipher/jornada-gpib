/* Host tests for the freestanding formatter and string helpers (src/rt). */
#include "check.h"
#include "rt/rt.h"

/* Build a 16-bit string from ASCII for test input. */
static unsigned short *W(const char *s, unsigned short *buf)
{
    unsigned int i = 0;
    while (s[i] != 0) {
        buf[i] = (unsigned short)(unsigned char)s[i];
        i++;
    }
    buf[i] = 0;
    return buf;
}

static int eq8(const char *got, const char *want)
{
    return strcmp(got, want) == 0;
}

static void fmt8(char *out, unsigned int cap, const char *fmt, ...)
{
    unsigned short wf[128];
    va_list ap;
    va_start(ap, fmt);
    rt_vfmt8(out, cap, W(fmt, wf), ap);
    va_end(ap);
}

static void test_numbers(void)
{
    char out[64];
    fmt8(out, sizeof out, "%d %u %x %X", -42, 42u, 0xbeefu, 0xbeefu);
    CHECK(eq8(out, "-42 42 beef BEEF"));
    fmt8(out, sizeof out, "[%5d][%-5d][%05d][%08X]", 7, 7, 7, 0x1234u);
    CHECK(eq8(out, "[    7][7    ][00007][00001234]"));
    fmt8(out, sizeof out, "%d %d", -2147483647 - 1, 2147483647);
    CHECK(eq8(out, "-2147483648 2147483647"));
    fmt8(out, sizeof out, "%u", 4294967295u);
    CHECK(eq8(out, "4294967295"));
    fmt8(out, sizeof out, "%%|%c|%i", 'Q', 5);
    CHECK(eq8(out, "%|Q|5"));
}

static void test_strings(void)
{
    char out[64];
    unsigned short ws[16];
    fmt8(out, sizeof out, "<%s><%S>", W("wide", ws), "narrow");
    CHECK(eq8(out, "<wide><narrow>"));
    fmt8(out, sizeof out, "[%6s][%-6S]", W("ab", ws), "cd");
    CHECK(eq8(out, "[    ab][cd    ]"));
    fmt8(out, sizeof out, "%s %S", (unsigned short *)0, (char *)0);
    CHECK(eq8(out, "(null) (null)"));
}

static void test_truncation_and_return_value(void)
{
    char out[8];
    unsigned short wf[32];
    unsigned short wout[8];
    int n;
    fmt8(out, sizeof out, "%S", "0123456789");
    CHECK(eq8(out, "0123456"));
    n = rt_fmt8(out, 0, W("abc", wf));
    CHECK_EQ_INT(n, 3);
    n = rt_fmt(wout, 8, W("%d-%d", wf), 12345, 6789);
    CHECK_EQ_INT(n, 10);
    CHECK(wout[7] == 0);
    CHECK(wout[0] == '1' && wout[6] == '6');
}

static void test_wide_output_and_helpers(void)
{
    unsigned short wf[32];
    unsigned short wout[32];
    unsigned short a[8];
    unsigned short b[8];
    int n = rt_fmt(wout, 32, W("%c%c%c", wf), 'a', 0x4e2d, 'z');
    CHECK_EQ_INT(n, 3);
    CHECK(wout[1] == 0x4e2d && wout[3] == 0);
    CHECK_EQ_INT(rt_wcslen(W("hello", a)), 5);
    CHECK_EQ_INT(rt_strlen("four"), 4);
    CHECK(rt_wcscmp(W("abc", a), W("abc", b)) == 0);
    CHECK(rt_wcscmp(W("abc", a), W("abd", b)) < 0);
    rt_wcscpy_n(wout, 4, W("toolong", a));
    CHECK(rt_wcslen(wout) == 3 && wout[0] == 't' && wout[2] == 'o');
    rt_wcscpy_n(wout, 0, W("x", a));
}

static void test_pointer(void)
{
    char out[32];
    fmt8(out, sizeof out, "%p", (void *)0x1a2b);
    CHECK(eq8(out, "0x00001a2b"));
}

int main(void)
{
    test_numbers();
    test_strings();
    test_truncation_and_return_value();
    test_wide_output_and_helpers();
    test_pointer();
    return check_summary("test_fmt");
}
