#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib6502.h"

M6502_Registers mpu_registers;
M6502_Memory mpu_memory;
M6502_Callbacks mpu_callbacks;
M6502 *mpu;

#define ROM_SIZE (16 * 1024)
uint8_t abe_roms[2][ROM_SIZE];

#define BASIC_LOMEM (0x0)
#define BASIC_HEAP (0x2)
#define BASIC_TOP (0x12)
#define BASIC_PAGE (0x18) // Just the high byte
#define BASIC_HIMEM (0x6)

int vdu_variables[257];

const char *osrdch_queue = 0;

void check(bool b, const char *s) {
    if (!b) {
        fprintf(stderr, "%s\n", s);
        exit(1);
    }
}

void check_alloc(void *p) {
    check(p != 0, "Unable to allocate memory");
}

void mpu_write_u16(uint16_t address, uint16_t data) {
    mpu_memory[address    ] = data & 0xff;
    mpu_memory[address + 1] = (data >> 8) & 0xff;
}

void mpu_clear_carry(M6502 *mpu) {
    mpu->registers->p &= ~(1<<0);
}

void mpu_dump(void) {
    char buffer[64];
    M6502_dump(mpu, buffer);
    fprintf(stderr, "%s\n", buffer);
}

void callback_abort(const char *type, uint16_t address, uint8_t data) {
    fprintf(stderr, "Unexpected %s at address %04x, data %02x\n", 
            type, address, data);
    // No point doing this, externalise() hasn't been called: mpu_dump();
    exit(1);
}

int callback_abort_read(M6502 *mpu, uint16_t address, uint8_t data) {
    callback_abort("read", address, data);
    exit(1); // prevent gcc warning
}

int callback_abort_write(M6502 *mpu, uint16_t address, uint8_t data) {
    callback_abort("write", address, data);
    exit(1); // prevent gcc warning
}

int callback_abort_call(M6502 *mpu, uint16_t address, uint8_t data) {
    callback_abort("call", address, data);
    exit(1); // prevent gcc warning
}

int callback_return_via_rts(M6502 *mpu) {
    uint8_t low  = mpu->memory[0x101 + mpu->registers->s];
    uint8_t high = mpu->memory[0x102 + mpu->registers->s];
    mpu->registers->s += 2;
    uint16_t address = (high << 8) | low;
    address += 1;
    fprintf(stderr, "SFTODOXXX %04x\n", address);
    return address;
}

int callback_osrdch(M6502 *mpu, uint16_t address, uint8_t data) {
    if (*osrdch_queue != '\0') {
        mpu->registers->a = *osrdch_queue;
        ++osrdch_queue;
        mpu_clear_carry(mpu);
        return callback_return_via_rts(mpu);
    }
    fprintf(stderr, "OSRDCH call with no keypress in queue\n");
    mpu_dump();
    exit(1);
}

// TODO: Output should ultimately be gated via a -v option, perhaps with some
// sort of (optional but default) filtering to tidy it up

int callback_oswrch(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu->registers->a;
    if (c == 0xa) {
        putchar('\n');
    } else if ((c >= ' ') && (c <= '~')) {
        putchar(c);
        //putchar('\n'); // SFTODO TEMP HACK
    }
    return callback_return_via_rts(mpu);
}

int callback_osasci(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu->registers->a;
    if (c == 0xd) {
        putchar('\n');
        return callback_return_via_rts(mpu);
    } else {
        return callback_oswrch(mpu, address, data);
    }
}

int callback_osnewl(M6502 *mpu, uint16_t address, uint8_t data) {
    putchar('\n');
    return callback_return_via_rts(mpu);
}

int callback_osbyte_return_u16(M6502 *mpu, uint16_t value) {
    mpu->registers->x = value & 0xff;
    mpu->registers->y = (value >> 8) & 0xff;
    return callback_return_via_rts(mpu);
}

int callback_osbyte_read_vdu_variable(M6502 *mpu) {
    int i = mpu->registers->x;
    if (vdu_variables[i] == -1) {
        fprintf(stderr, "Unsupported VDU variable read: %02x\n", i);
        mpu_dump();
        exit(1);
    }
    if (vdu_variables[i + 1] == -1) {
        fprintf(stderr, "Unsupported VDU variable read: %02x\n", i + 1);
        mpu_dump();
        exit(1);
    }
    mpu->registers->x = vdu_variables[i];
    mpu->registers->y = vdu_variables[i + 1];
    return callback_return_via_rts(mpu);
}

int callback_osbyte(M6502 *mpu, uint16_t address, uint8_t data) {
    switch (mpu->registers->a) {
        case 0x03: // select output device
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x0f: // flush buffers
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x7c: // clear ESCAPE condition
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x84: // read HIMEM
            return callback_osbyte_return_u16(mpu, 0x8000); // TODO: MAGIC CONST
        case 0x86: // read text cursor position
            return callback_osbyte_return_u16(mpu, 0); // TODO: MAGIC CONST, HACK
        case 0xa0:
            return callback_osbyte_read_vdu_variable(mpu);
        default:
            fprintf(stderr, "Unsupported OSBYTE: A=%02x, X=%02x, Y=%02x\n",
                    mpu->registers->a, mpu->registers->x, mpu->registers->y);
            mpu_dump();
            exit(1);
    }
}

int callback_read_escape_flag(M6502 *mpu, uint16_t address, uint8_t data) {
    return 0; // Escape flag not set
}

int callback_romsel_write(M6502 *mpu, uint16_t address, uint8_t data) {
    switch (data) {
        case 0:
        case 1:
            memcpy(&mpu_memory[0x8000], abe_roms[data], ROM_SIZE);
            break;
        default:
            check(false, "Invalid ROM bank selected");
            break;
    }
    return 0; // return value ignored
}

void callback_poll(M6502 *mpu) {
}

void set_abort_callback(uint16_t address) {
    M6502_setCallback(mpu, read,  address, callback_abort_read);
    // TODO: Get rid of write callback permanently?
    //M6502_setCallback(mpu, write, address, callback_abort_write);
}

void load_rom(const char *filename, uint8_t *data) {
    FILE *file = fopen(filename, "rb");
    check(file != 0, "Can't find ABE ROM image");
    size_t items = fread(data, ROM_SIZE, 1, file);
    check(items == 1, "ABE ROM image is too short");
    fclose(file);
}

void init(void) {
    mpu = M6502_new(&mpu_registers, mpu_memory, &mpu_callbacks);
    check_alloc(mpu);
    M6502_reset(mpu);

    // Install handlers to abort on read or write of anywhere in language or OS
    // workspace; this will catch anything we haven't explicitly implemented,
    // since BASIC isn't actually running on the emulated machine.
    for (uint16_t address = 0; address < 0x100; ++address) {
        switch (address) {
            case BASIC_LOMEM:
            case BASIC_LOMEM + 1:
            case BASIC_HEAP:
            case BASIC_HEAP + 1:
            case BASIC_TOP:
            case BASIC_TOP + 1:
            case BASIC_PAGE:
            case BASIC_HIMEM:
            case BASIC_HIMEM + 1:
            case 0x0a: // pragmatic
            case 0x0b: // pragmatic
            case 0x0c: // pragmatic
            case 0x2a: // pragmatic
            case 0x2b: // pragmatic
            case 0x2c: // pragmatic
            case 0x2d: // pragmatic
            case 0x37: // pragmatic
            case 0x38: // pragmatic
            case 0x39: // pragmatic
            case 0x3a: // pragmatic
            case 0x3b: // pragmatic
            case 0x3d: // pragmatic
            case 0x3e: // pragmatic
            case 0x3f: // pragmatic
            case 0x47: // pragmatic
            case 0xa8:
            case 0xa9:
            case 0xf2:
            case 0xf3:
            case 0xf4:
                // Supported as far as necessary, don't install a handler.
                break;
            default:
                if ((address >= 0x70) && (address <= 0xaf)) {
                    // Supported as far as necessary, don't install a handler.
                } else {
                    set_abort_callback(address);
                }
                break;
        }
    }
    // Trap access to unimplemented OS vectors.
    for (uint16_t address = 0x200; address < 0x236; ++address) {
        switch (address) {
            case 0x20e:
            case 0x20f:
                // Supported as far as necessary, don't install a handler.
                break;
            default:
                set_abort_callback(address);
                break;
        }
    }
#if 0 // SFTODO!?
    // TODO: If things aren't working, perhaps tighten this up - I'm assuming
    // pages 5/6/7 don't contain interesting BASIC housekeeping data for ABE.
    for (uint16_t address = 0x400; address < 0x500; ++address) {
        set_abort_callback(address);
    }
#endif

    // Install handlers for OS entry points, using a default for unimplemented
    // ones.
    for (uint16_t address = 0xc000; address != 0; ++address) {
        M6502_setCallback(mpu, call, address, callback_abort_call);
    }
    M6502_setCallback(mpu, call, 0xffe0, callback_osrdch);
    M6502_setCallback(mpu, call, 0xffe3, callback_osasci);
    M6502_setCallback(mpu, call, 0xffe7, callback_osnewl);
    M6502_setCallback(mpu, call, 0xffee, callback_oswrch);
    M6502_setCallback(mpu, call, 0xfff4, callback_osbyte);

    // Install fake OS vectors. Because of the way our implementation works,
    // these vectors actually point to the official entry points.
    mpu_write_u16(0x20e, 0xffee);

    // Since we don't have an actual ESCAPE handler, just ensure any read from
    // &ff always returns 0.
    M6502_setCallback(mpu, read, 0xff, callback_read_escape_flag);

    // Install handler for hardware emulation.
    M6502_setCallback(mpu, write, 0xfe30, callback_romsel_write);

    // Set up VDU variables.
    for (int i = 0; i < 256; ++i) {
        vdu_variables[i] = -1;
    }
    vdu_variables[0x55] = 7; // screen mode
    vdu_variables[0x56] = 4; // memory map type: 1K mode

    // Load the ABE ROMs.
    // TODO: Subject to permission it might be nice to build these into the
    // executable to avoid awkward "where to load them from" PATH-type issues.
    load_rom("roms/EDITORA100.rom", abe_roms[0]);
    load_rom("roms/EDITORB100.rom", abe_roms[1]);
}

void load_basic(const char *filename) {
    FILE *file = fopen(filename, "rb");
    check(file != 0, "Can't open input");
    const uint16_t page = 0xe00;
    const uint16_t himem = 0x8000;
    size_t length = fread(&mpu_memory[page], 1, himem - page, file);
    check(!ferror(file), "Error reading input");
    check(feof(file), "Input is too large");
    fclose(file);
    uint16_t top = page + length;
    mpu_memory[BASIC_PAGE] = (page >> 8) & 0xff;
    mpu_write_u16(BASIC_TOP, top);
    mpu_write_u16(BASIC_LOMEM, top);
    mpu_write_u16(BASIC_HEAP, top); // SFTODO!?
    mpu_write_u16(BASIC_HIMEM, himem);
    // TODO: Will need to set up some zp pointers to PAGE/TOP/whatever
#if 1 // SFTODO
    {
    FILE *file = fopen("mem.tmp", "wb");
    fwrite(mpu_memory, 1, 64 * 1024, file);
    fclose(file);
    }
#endif
}

void make_service_call(void) {
    const uint16_t command_address = 0xa00;
    strcpy((char *) &mpu_memory[command_address], "BUTIL\x0d");
    mpu_memory[0xf2] = command_address & 0xff;
    mpu_memory[0xf3] = (command_address >> 8) & 0xff;

    mpu_registers.a = 4; // unrecognised * command
    mpu_registers.x = 1; // current ROM bank
    mpu_registers.y = 0; // command tail offset

    const uint16_t code_address = 0x900;
    uint8_t *p = &mpu_memory[code_address];
                                           // .loop
    *p++ = 0x86; *p++ = 0xf4;              // STX &F4
    *p++ = 0x8e; *p++ = 0x30; *p++ = 0xfe; // STX &FE30
    *p++ = 0x20; *p++ = 0x03; *p++ = 0x80; // JSR &8003 (service call handler)
    *p++ = 0xa6; *p++ = 0xf4;              // LDX &F4
    *p++ = 0xca;                           // DEX
    *p++ = 0x10; *p++ = 256 - 13;          // BPL loop
    *p++ = 0x00;                           // BRK

    mpu_registers.s  = 0xff;
    mpu_registers.pc = code_address; // TODO: why magic +1?
#if 1 // TODO TEMP DEBUG
    char buffer[100];
    M6502_dump(mpu, buffer);
    fprintf(stderr, "%s\n", buffer);
    uint16_t addr = 0x900;
    for (int i = 16; i > 0; --i) {
    addr += M6502_disassemble(mpu, addr, buffer);
    fprintf(stderr, "%s\n", buffer);
    }
#endif
    M6502_run(mpu, callback_poll); // never returns
}

int main(int argc, char *argv[]) {
    // TODO: Super-crude!
    check(argc == 2, "No filename given!");
    init();
    load_basic(argv[1]);
    // TODO: The different options should be exposed via command line switches
    osrdch_queue = 
        "P" // Pack
        "Y" // REMs?
        "Y" // Spaces?
        "Y" // Comments?
        "Y" // Variables?
        "Y" // Use unused singles?
        "Y" // Concatenate?
        ;
    make_service_call();
}

// vi: colorcolumn=80
