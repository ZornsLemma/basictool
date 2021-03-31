#include "utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// TODO: Entirely experimental function, not yet used
void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

// TODO: I should probably extend check to be printf-like and make all callers use this where helpful
void check(bool b, const char *s) {
    if (!b) {
        die("%s", s);
    }
}

void *check_alloc(void *p) {
    check(p != 0, "Error: Unable to allocate memory");
    return p;
}
