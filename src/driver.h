#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

// The emulation layer effectively forwards calls to OSWRCH onto this function.
extern void pending_output_insert(uint8_t data); // TODO!

// TODO: COMMENT!
void load_basic(const char *filename); // TODO!
void pack(void); // TODO!
void renumber(void); // TODO!
void save_basic(const char *filename); // TODO!
void save_ascii_basic(const char *filename); // TODO!
void save_formatted_basic(const char *filename); // TODO!
void save_line_ref(const char *filename); // TODO!
void save_variable_xref(const char *filename); // TODO!

// vi: colorcolumn=80

#endif
