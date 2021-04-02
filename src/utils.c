#include "utils.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"

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

int max(int lhs, int rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

char *ourstrdup(const char *s) {
    char *t = malloc(strlen(s) + 1);
    strcpy(t, s);
    return t;
}

FILE *fopen_wrapper(const char *pathname, const char *mode) {
    assert(pathname != 0);
    assert(mode != 0);

    bool read;
    switch (mode[0]) {
        case 'r':
            read = true;
            break;
        case 'w':
            read = false;
            break;
        default:
            die("Internal error: Invalid mode \"%s\" passed to fopen_wrapper()", mode);
            break;
    }

    if (strcmp(pathname, "-") == 0) {
        // We ignore the presence of a "b" in mode; I don't think there's a
        // portable way to re-open stdin/stdout in binary mode. TODO: Should we
        // generate an error if there's a "b"? But this is probably fine on
        // Unix-like systems.
        return read ? stdin : stdout;
    } else {
        FILE *file = fopen(pathname, mode);
        check(file != 0, "Error: Can't open %s file \"%s\"", read ? "input" : "output", pathname);
        return file;
    }
}
// vi: colorcolumn=80
