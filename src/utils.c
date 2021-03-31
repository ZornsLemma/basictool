#include "utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// TODO: Entirely experimental function, not yet used
NORETURN static void die_internal(const char *fmt, va_list ap) {
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    die_internal(fmt, ap);
}

// TODO: I should probably extend check to be printf-like and make all callers use this where helpful
void check(bool b, const char *fmt, ...) {
    if (!b) {
        va_list ap;
        va_start(ap, fmt);
        die_internal(fmt, ap);
    }
}

void *check_alloc(void *p) {
    check(p != 0, "Error: Unable to allocate memory");
    return p;
}
