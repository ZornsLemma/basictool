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

