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

void check(bool b, const char *s) {
    if (!b) {
        fprintf(stderr, "%s\n", s);
        exit(1);
    }
}

void check_alloc(void *p) {
    check(p != 0, "Unable to allocate memory");
}

void callback_abort(const char *type, uint16_t address, uint8_t data) {
    fprintf(stderr, "Unexpected %s at address %04x, data %02x\n", 
            type, address, data);
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
    M6502_setCallback(mpu, write, address, callback_abort_write);
}

void load_rom(const char *filename, uint8_t *data) {
    FILE *file = fopen(filename, "rb");;
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
            case 0xf4:
                // Supported as far as necessary, don't install a handler.
                break;
            default:
                set_abort_callback(address);
                break;
        }
    }
    for (uint16_t address = 0x400; address < 0x800; ++address) {
        set_abort_callback(address);
    }

    M6502_setCallback(mpu, write, 0xfe30, callback_romsel_write);

    // Load the ABE ROMs.
    // TODO: Subject to permission it might be nice to build these into the
    // executable to avoid awkward "where to load them from" PATH-type issues.
    load_rom("roms/EDITORA100.rom", abe_roms[0]);
    load_rom("roms/EDITORB100.rom", abe_roms[1]);
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
    init();
    make_service_call();
}

// vi: colorcolumn=80
