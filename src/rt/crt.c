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

/* Modulo helpers. GCC calls these with the ordinary C convention; the quotient routines
 * they use (__udivsi3, __sdivsi3) live in divide.s because GCC calls those with a special
 * register contract on SH-3. */
unsigned int __udivsi3(unsigned int n, unsigned int d);
int __sdivsi3(int n, int d);

unsigned int __umodsi3(unsigned int n, unsigned int d)
{
    if (d == 0) {
        return 0;
    }
    return n - __udivsi3(n, d) * d;
}

int __modsi3(int n, int d)
{
    if (d == 0) {
        return 0;
    }
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
