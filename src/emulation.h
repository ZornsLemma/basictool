#ifndef EMULATION_H
#define EMULATION_H

#include "lib6502.h"

static const uint16_t page = 0xe00;
static const uint16_t himem = 0x8000;

M6502_Memory mpu_memory;

// TODO: TIDY/REORDER/COMMENT?
void emulation_init(void);
// TODO: Rename next two functions emulation_* not execute_*? Or other naming change?
void execute_input_line(const char *line);
void execute_osrdch(const char *s);
uint16_t mpu_read_u16(uint16_t address);

#endif
