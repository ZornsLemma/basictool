#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

// The emulation layer effectively forwards calls to OSWRCH onto this function.
extern void pending_output_insert(uint8_t data); // TODO!

// Load a BASIC program from 'filename' into the emulated machine's memory,
// tokenising it if necessary. We will auto-detect whether or not the program
// is already tokenised, unless config.input_tokenised tells us to assume
// it's tokenised.
void load_basic(const char *filename);

// TODO: COMMENT ON ALL FOLLOWING
void pack(void); // TODO!
void renumber(void); // TODO!

// Save the BASIC program in the emulated machine's memory to 'filename' in
// tokenised format.
void save_tokenised_basic(const char *filename); // TODO!

// Save the BASIC program in the emulated machine's memory to 'filename' in
// ASCII format, using LISTO option config.listo to control formatting.
void save_ascii_basic(const char *filename); // TODO!

void save_formatted_basic(const char *filename); // TODO!
void save_line_ref(const char *filename); // TODO!
void save_variable_xref(const char *filename); // TODO!

// vi: colorcolumn=80

#endif
