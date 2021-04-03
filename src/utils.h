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

// Constants for carriage return (CR) and line feed (LF). These are short
// names for globals but we use them a lot.
//
// We could probably just use '\r' and '\n' because we're already
// assuming the C environment uses ASCII, but let's be paranoid - I could
// vaguely imagine an ASCII C environment where '\r' is 10 and '\n' is
// 13 because it makes writing to native text files with CR line edings
// via the C library less of a special case. (The draft C99 standard
// (http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf) says the value
// of these escape sequences is implementation-defined; see section 5.2.2.3.)
static const char cr = 13;
static const char lf = 10;

// Line number in input file to display on any error messages; if this is -1
// the error is assumed to be independent of any particular line in the file.
extern int error_line_number;

// Write a suitable error message prefix (which may be empty) to stderr; in
// practice this will write nothing if error_line_number is -1, otherwise it
// will write a gcc-style filename:lineno: prefix.
void print_error_prefix(void);

// printf-like function which adds an "info" prefix and writes to stderr.
void info(const char *fmt, ...) PRINTFLIKE(1, 2);

// Write a "warning" prefix and 's' to stderr.
void warn(const char *fmt, ...) PRINTFLIKE(1, 2);

// Like fprintf(stderr, fmt, ...) except:
// - a newline will automatically be appended
// - exit(EXIT_FAILURE) will be called afterwards
void die(const char *fmt, ...) PRINTFLIKE(1, 2) NORETURN;

// Like die(), but appending a line advising the user to try --help.
void die_help(const char *fmt, ...) PRINTFLIKE(1, 2) NORETURN;

// If b is false, call die(fmt, ...).
void check(bool b, const char *fmt, ...) PRINTFLIKE(2, 3);

// Return p if it's not null, otherwise die() with an "out of memory" error.
void *check_alloc(void *p);

// Return the larger of lhs and rhs.
int max(int lhs, int rhs);

// A simple implementation of strdup() so we don't assume it's available; it's
// not part of C99.
char *ourstrdup(const char *s);

// A wrapper for fopen() which automatically converts "-" to stdin/stdout and
// calls die() if any errors occur, so the return value can't be null.
FILE *fopen_wrapper(const char *pathname, const char *mode);

// Read a binary file into a malloc()-ed block of memory. The pointer to
// the malloc()-ed block is returned and *length is set to the length.
char *load_binary(const char *filename, size_t *length);

// Get the next line of text from a binary data block, which must have been
// loaded with load_binary(). CR, LF, LFCR and CRLF line termination is
// automatically detected. The arguments are pointers so they can be updated
// as this is called. Return a pointer to the line or null at "EOF".
char *get_line(char **data_ptr, size_t *length_ptr);

// vi: colorcolumn=80

#endif
