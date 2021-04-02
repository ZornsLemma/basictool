#ifndef EMULATION_H
#define EMULATION_H

#include "lib6502.h"

static const uint16_t page = 0xe00;
static const uint16_t himem = 0x8000;

// Code outside the emulation is free to read/write the emulated machine's
// memory directly.
M6502_Memory mpu_memory;

// Read a little-endian 16-bit word from the emulated machine's memory.
uint16_t mpu_read_u16(uint16_t address);

// Initialise the emulated machine; this will return to the caller with the
// emulated machine waiting at the BASIC prompt.
void emulation_init(void);

// The next two functions rely on the caller to know the OS input routine
// the emulated machine is waiting in. In practice this isn't a problem -
// the driver code needs to be quite familiar with the specifics of the code
// being run on the emulated machine - but of course it would be possible
// to provide a generic "type this" function which hides this detail.

// TODO: Rename next two functions emulation_* not execute_*? Or other naming change?

// Enter 'line' as if typed at the keyboard; 'line' should not be terminated
// with CR or LF, this function will automatically append CR. This assumes
// the emulated machine is waiting for input via OSWORD 0; the caller is
// responsible for ensuring that is the case.
void execute_input_line(const char *line);

// Enter s[0] as if typed at the keyboard; 's' should be a single-character
// string. This assumes the emulated machine is waiting for input via OSRDCH;
// the caller is responsible for ensuring that is the case.
void execute_osrdch(const char *s);

// vi: colorcolumn=80

#endif
