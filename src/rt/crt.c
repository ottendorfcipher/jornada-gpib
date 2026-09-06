/* Freestanding runtime for code built with sh-elf GCC and linked without any C library.
 *
 * GCC may emit calls to memcpy/memset/memcmp for struct copies and initialisers, so these
 * must exist even though no header declares them for application code. They are compiled
 * with GCC's own convention and only ever called from GCC-compiled code. Host test builds
 * (RT_HOST_TEST) use the platform C library and only compile the rt_* helpers below.
 */
#include "rt/rt.h"

#ifndef RT_HOST_TEST
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a;
    const unsigned char *y = b;
    while (n--) {
        if (*x != *y) {
            return (int)*x - (int)*y;
        }
        x++;
        y++;
    }
    return 0;
}

/* Integer division helpers. SH-3 has no divide instruction and this project does not link
 * libgcc, so GCC's calls to __udivsi3 / __sdivsi3 / __umodsi3 / __modsi3 resolve here.
 * Restoring shift-subtract division; speed is irrelevant for our use (timeouts, formatting). */
unsigned int __udivsi3(unsigned int n, unsigned int d)
{
    unsigned int q = 0;
    unsigned int r = 0;
    int i;
    if (d == 0) {
        return 0;
    }
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1u << i;
        }
    }
    return q;
}

unsigned int __umodsi3(unsigned int n, unsigned int d)
{
    if (d == 0) {
        return 0;
    }
    return n - __udivsi3(n, d) * d;
}

int __sdivsi3(int n, int d)
{
    unsigned int un = (unsigned int)(n < 0 ? -n : n);
    unsigned int ud = (unsigned int)(d < 0 ? -d : d);
    unsigned int q = __udivsi3(un, ud);
    return ((n < 0) != (d < 0)) ? -(int)q : (int)q;
}

int __modsi3(int n, int d)
{
    return n - __sdivsi3(n, d) * d;
}
#endif /* RT_HOST_TEST */

unsigned int rt_wcslen(const unsigned short *s)
{
    unsigned int n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

unsigned int rt_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

int rt_wcscmp(const unsigned short *a, const unsigned short *b)
{
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

unsigned short *rt_wcscpy_n(unsigned short *dst, unsigned int cap, const unsigned short *src)
{
    unsigned int i = 0;
    if (cap == 0) {
        return dst;
    }
    while (i + 1 < cap && src[i] != 0) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    return dst;
}
