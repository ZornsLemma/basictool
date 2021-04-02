#include "utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// TODO: Ideally we would include this in *any* error message if it's not -1,
// but it may be OK if it's a lot cleaner/easier to just do it in carefully
// selected places.
int error_line_number = -1;

void print_error_filename_prefix(void) {
    if (error_line_number >= 1) {
        fprintf(stderr, "%s:%d: ", filenames[0] ? filenames[0] : "-",
                error_line_number);
    }
}

// TODO: Entirely experimental function, not yet used
NORETURN static void die_internal(const char *fmt, va_list ap) {
    print_error_filename_prefix();
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

// vi: colorcolumn=80
