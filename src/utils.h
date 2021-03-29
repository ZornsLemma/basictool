#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>

#ifdef __GNUC__
#define PRINTFLIKE(string_index, first_to_check) \
    __attribute__((format(printf, string_index, first_to_check)))
#define NORETURN __attribute__((noreturn))
#else
#define PRINTFLIKE(string_index, first_to_check)
#define NORETURN
#endif

// TODO: Entirely experimental function, not yet used
// Like fprintf(stderr, fmt, ...) except:
// - a newline will automatically be appended
// - exit(EXIT_FAILURE) will be called afterwards
void die(const char *fmt, ...) PRINTFLIKE(1, 2) NORETURN;

// If b is false, call die("%s", s).
void check(bool b, const char *s);

#endif
