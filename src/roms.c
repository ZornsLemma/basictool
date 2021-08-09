#include "roms.h"

const uint8_t rom_editor_a[] = {
#include "zz-editor-a.c"
};

const uint8_t rom_editor_b[] = {
#include "zz-editor-b.c"
};

const uint8_t rom_basic[basic_count][rom_size] = {
    {
#include "zz-basic-2.c"
    },
    {
#include "zz-basic-4.c"
    }
};

// vi: colorcolumn=80
