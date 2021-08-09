#ifndef ROMS_H
#define ROMS_H

#include <stdint.h>
#include <stddef.h>

enum {
    bank_editor_a =  0,
    bank_editor_b =  1,
    bank_basic    = 12  // same as Master 128, although it's arbitrary really
};

enum {
    basic_2,
    basic_4,
    basic_count
};

enum {
    rom_size = 16 * 1024
};

// TODO: Rename rom_editor to rom_abe? Ditto bank_editor. Or maybe I should
// be calling it "bet", *HELP calls it "BASIC Editor & Toolkit".
extern const uint8_t rom_editor_a[];
extern const uint8_t rom_editor_b[];
extern const uint8_t rom_basic[basic_count][rom_size];

// vi: colorcolumn=80

#endif
