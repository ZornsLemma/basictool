#ifndef ROMS_H
#define ROMS_H

#include <stdint.h>

enum {
    bank_editor_a =  0,
    bank_editor_b =  1,
    bank_basic    = 12  // same as Master 128, although it's arbitrary really
};

// TODO: Rename rom_editor to rom_abe? Ditto bank_editor
extern const uint8_t rom_editor_a[];
extern const uint8_t rom_editor_b[];
extern const uint8_t rom_basic[];

#endif
