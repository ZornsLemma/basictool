#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

// The emulation layer effectively forwards calls to OSWRCH onto this function.
extern void driver_oswrch(uint8_t data);

// Load a BASIC program from 'filename' into the emulated machine's memory,
// tokenising it if necessary. We will auto-detect whether or not the program
// is already tokenised, unless config.input_tokenised tells us to assume
// it's tokenised.
void load_basic(const char *filename);

// Pack the BASIC program in the emulated machine's memory using ABE's "Pack"
// command.
void pack(void);

// Renumber the BASIC program in the emulated machine's memory using BASIC's
// RENUMBER command, with arguments taken from 'config'.
void renumber(void);

// Save the BASIC program in the emulated machine's memory to 'filename' in
// tokenised format.
void save_tokenised_basic(const char *filename);

// Save the BASIC program in the emulated machine's memory to 'filename' in
// ASCII format, using LISTO option config.listo to control formatting.
void save_ascii_basic(const char *filename);

// Save the BASIC program in the emulated machine's memory to 'filename' in
// ASCII format, using ABE's "format" option to print it.
void save_formatted_basic(const char *filename);

// Save the output of ABE's "Table line references" command on the BASIC
// program in the emulated machine's memory to 'filename'.
void save_line_ref(const char *filename);

// Save the output of ABE's "Variables Xref" command on the BASIC
// program in the emulated machine's memory to 'filename'.
void save_variable_xref(const char *filename);

// vi: colorcolumn=80

#endif
