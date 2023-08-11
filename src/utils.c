#include "utils.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "main.h"

int error_line_number = -1;

void print_error_prefix(void) {
    if (error_line_number >= 1) {
        fprintf(stderr, "%s:%d: ", filenames[0] ? filenames[0] : "-",
                error_line_number);
    }
}

void info(const char *fmt, ...) {
    fprintf(stderr, "info: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
}

void warn(const char *fmt, ...) {
    fprintf(stderr, "warning: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
}

static void die_internal(const char *fmt, va_list ap) {
    print_error_prefix();
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
}

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    die_internal(fmt, ap);
    exit(EXIT_FAILURE);
}

void die_help(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    die_internal(fmt, ap);
    fprintf(stderr, "Try \"%s --help\" for more information.\n", program_name);
    exit(EXIT_FAILURE);
}

void check(bool b, const char *fmt, ...) {
    if (!b) {
        va_list ap;
        va_start(ap, fmt);
        die_internal(fmt, ap);
        exit(EXIT_FAILURE);
    }
}

void *check_alloc(void *p) {
    check(p != 0, "error: unable to allocate memory");
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
    assert(strlen(mode) <= 2);

    bool read;
    switch (mode[0]) {
        case 'r':
            read = true;
            break;
        case 'w':
            read = false;
            break;
        default:
            die("internal error: invalid mode \"%s\" passed to fopen_wrapper()", mode);
            break;
    }

    if (config.open_output_binary && !read) {
        mode = "wb";
    }

    if (strcmp(pathname, "-") == 0) {
        // We ignore the presence of a "b" in mode; I don't think there's a
        // portable way to re-open stdin/stdout in binary mode. TODO: Should we
        // generate an error if there's a "b"? But this is probably fine on
        // Unix-like systems.
        return read ? stdin : stdout;
    } else {
        FILE *file = fopen(pathname, mode);
        check(file != 0, "error: can't open %s file \"%s\"", read ? "input" : "output", pathname);
        return file;
    }
}

char *load_binary(const char *filename, size_t *length) {
    assert(length != 0);
    FILE *file = fopen_wrapper(filename, "rb");
    // Since we're dealing with BASIC programs on a 32K-ish machine, we don't
    // need to handle arbitrarily large files.
    const int max_size = 64 * 1024;
    char *data = check_alloc(malloc(max_size));
    *length = fread(data, 1, max_size, file);
    check(!ferror(file), "error: error reading from input file \"%s\"",
          filename);
    check(feof(file), "error: input file \"%s\" is too large", filename);
    check(fclose(file) == 0, "error: error closing input file \"%s\"",
          filename);
    // Shrink the allocated block down from max_size to the size we actually
    // need. We secretly allocate an extra byte for get_line() to use in case
    // the last line of a text file doesn't have a terminator.
    return check_alloc(realloc(data, *length + 1));
}

char *get_line(char **data_ptr, size_t *length_ptr) {
    assert(data_ptr != 0);
    assert(length_ptr != 0);
    char *data = *data_ptr;
    assert(data != 0);
    size_t length = *length_ptr;

    if (length == 0) {
        return 0;
    }

    // Find the end of the line.
    char *p = data;
    while ((length > 0) && (*p != cr) && (*p != lf)) {
        ++p; --length;
    }
    if (length <= 1) {
        if (length == 0) {
            // The last line of text isn't terminated, so we need to write a
            // NUL at *p after the last character. This would write past the
            // end of the buffer, but luckily load_binary() allocated a secret
            // extra byte for exactly this eventuality.
        } else {
            assert((*p == cr) || (*p == lf));
        }
        *p = '\0';
        *data_ptr = p; // doesn't really matter as length is now 0
        *length_ptr = 0;
        return data;
    }

    char terminator = *p;
    *p = '\0';
    ++p; --length;
    assert(length > 0);

    // If the next character is the opposite terminator, skip it. This allows
    // us to handle CR, LF, LFCR or CRLF-terminated lines.
    const char opposite_terminator = (terminator == cr) ? lf : cr;
    if (*p == opposite_terminator) {
        ++p; --length;
    }

    *data_ptr = p;
    *length_ptr = length;
    return data;
}

// vi: colorcolumn=80
