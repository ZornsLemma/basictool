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

void info(const char *fmt, ...) {
    fprintf(stderr, "Info: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
}

void warn(const char *s) {
    fprintf(stderr, "Warning: %s\n", s);
}

// TODO: Entirely experimental function, not yet used
NORETURN static void die_internal(const char *fmt, va_list ap) {
    print_error_filename_prefix();
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
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

char *load_binary(const char *filename, size_t *length) {
    assert(length != 0);
    FILE *file = fopen_wrapper(filename, "rb");
    // Since we're dealing with BASIC programs on a 32K-ish machine, we don't
    // need to handle arbitrarily large files.
    const int max_size = 64 * 1024;
    char *data = check_alloc(malloc(max_size));
    *length = fread(data, 1, max_size, file);
    check(!ferror(file), "Error: Error reading from input file \"%s\"", filename);
    check(feof(file), "Error: Input file \"%s\" is too large", filename);
    check(fclose(file) == 0, "Error: Error closing input file \"%s\"", filename);
    // We allocate an extra byte so we can easily guarantee that the last line
    // ends with a line terminator when reading non-tokenised input.
    return check_alloc(realloc(data, (*length) + 1));
}

// TODO: Review this later, I think there are no missing corner cases (bearing in mind we deliberately put a CR at the end of the input to catch unterminated last lines) but a fresh look would be good
char *get_line(char **data_ptr, size_t *length_ptr) {
    assert(data_ptr != 0);
    assert(length_ptr != 0);
    char *data = *data_ptr;
    size_t length = *length_ptr;

    if (length == 0) {
        return 0;
    }

    // Find the end of the line.
    char *eol = data;
    while ((*eol != 0x0d) && (*eol != 0x0a)) {
        ++eol; --length;
    }
    assert(length > 0);
    char terminator = *eol;
    *eol = '\0'; --length;

    // If there is a next character and it's the opposite terminator, skip it.
    // This allows us to handle CR, LF, LFCR or CRLF-terminated lines.
    char *next_line = eol;
    if (length > 0) {
        ++next_line;
        const char opposite_terminator = (terminator == 0x0d) ? 0x0a: 0x0d;
        if (*next_line == opposite_terminator) {
            ++next_line; --length;
        }
    }
    *data_ptr = next_line;
    *length_ptr = length;
    return data;
}

// vi: colorcolumn=80
