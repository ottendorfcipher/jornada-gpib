/* Freestanding runtime helpers shared by every target (device code and host tests).
 *
 * On the SH-3 target there is no C library: crt.c supplies memcpy and friends. Host test
 * builds define RT_HOST_TEST and use the platform C library instead.
 */
#ifndef RT_RT_H
#define RT_RT_H

#include <stdarg.h>
#include <stddef.h>

#ifdef RT_HOST_TEST
#include <string.h>
#else
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif

unsigned int rt_wcslen(const unsigned short *s);
unsigned int rt_strlen(const char *s);
int rt_wcscmp(const unsigned short *a, const unsigned short *b);
unsigned short *rt_wcscpy_n(unsigned short *dst, unsigned int cap, const unsigned short *src);

/* Minimal printf-style formatting into a 16-bit character buffer (Windows CE strings).
 * Conversions: %d %i %u %x %X %c %s (16-bit string) %S (8-bit string) %p %%; flags: '-', '0',
 * width. Always NUL-terminates when cap > 0. Returns the number of characters that would have
 * been written, excluding the terminator (like snprintf). */
int rt_vfmt(unsigned short *buf, unsigned int cap, const unsigned short *fmt, va_list ap);
int rt_fmt(unsigned short *buf, unsigned int cap, const unsigned short *fmt, ...);

/* Same, into an 8-bit buffer (used for log files). %s still takes a 16-bit string. */
int rt_vfmt8(char *buf, unsigned int cap, const unsigned short *fmt, va_list ap);
int rt_fmt8(char *buf, unsigned int cap, const unsigned short *fmt, ...);

#endif /* RT_RT_H */
