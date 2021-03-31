#ifndef DRIVER_H
#define DRIVER_H

// The emulation layer effectively forwards calls to OSWRCH onto this function.
extern void pending_output_insert(uint8_t data); // TODO!

// vi: colorcolumn=80

#endif
