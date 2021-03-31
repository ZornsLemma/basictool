#ifndef EMULATION_H
#define EMULATION_H

#include "lib6502.h"

static const uint16_t page = 0xe00;
static const uint16_t himem = 0x8000;

M6502_Memory mpu_memory;

extern void pending_output_insert(uint8_t data); // TODO!

// TODO: TIDY/REORDER/COMMENT?
void init(void);
void execute_input_line(const char *line);
void execute_osrdch(const char *s);
uint16_t mpu_read_u16(uint16_t address);

#endif
