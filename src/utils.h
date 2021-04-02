#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __GNUC__
#define PRINTFLIKE(string_index, first_to_check) \
    __attribute__((format(printf, string_index, first_to_check)))
#define NORETURN __attribute__((noreturn))
#else
#define PRINTFLIKE(string_index, first_to_check)
#define NORETURN
#endif

// TODO: COMMENT IF KEEP
extern int error_line_number;

// TODO: COMMENT IF KEEP
void print_error_filename_prefix(void);

// TODO: Entirely experimental function, not yet used
// Like fprintf(stderr, fmt, ...) except:
// - a newline will automatically be appended
// - exit(EXIT_FAILURE) will be called afterwards
void die(const char *fmt, ...) PRINTFLIKE(1, 2) NORETURN;

// If b is false, call die(s, ...).
void check(bool b, const char *fmt, ...) PRINTFLIKE(2, 3);

// TODO: COMMENT
void *check_alloc(void *p);

// Return the larger of lhs and rhs.
int max(int lhs, int rhs);

// A simple implementation of strdup() so we don't assume it's available; it's
// not part of C99.
char *ourstrdup(const char *s);

// A wrapper for fopen() which automatically converts "-" to stdin/stdout and
// calls die() if any errors occur, so the return value can't be null.
FILE *fopen_wrapper(const char *pathname, const char *mode);

// vi: colorcolumn=80

#endif
