/* Tiny assertion helper for the host C tests: no framework, just counts and a summary. */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <stdlib.h>

static int check_failures = 0;
static int check_total = 0;

#define CHECK(cond) do { \
        check_total++; \
        if (!(cond)) { \
            check_failures++; \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_EQ_INT(a, b) do { \
        long _a = (long)(a), _b = (long)(b); \
        check_total++; \
        if (_a != _b) { \
            check_failures++; \
            fprintf(stderr, "%s:%d: CHECK_EQ_INT failed: %s = %ld, %s = %ld\n", __FILE__, __LINE__, #a, _a, #b, _b); \
        } \
    } while (0)

static int check_summary(const char *name)
{
    printf("%s: %d checks, %d failures\n", name, check_total, check_failures);
    return check_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif
