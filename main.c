#include <stdbool.h>
#include <stdlib.h>
#include "lib6502.h"

M6502_Registers mpu_registers;
M6502_Memory mpu_memory;
M6502_Callbacks mpu_callbacks;
M6502 *mpu;

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
    fprintf(stderr, "Unexpected %s at address %04x, data %02x", 
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

void set_abort_callback(uint16_t address) {
    M6502_setCallback(mpu, read,  address, callback_abort_read);
    M6502_setCallback(mpu, write, address, callback_abort_write);
}

void init() {
    mpu = M6502_new(&mpu_registers, mpu_memory, &mpu_callbacks);
    check_alloc(mpu);

    // Install handlers to abort on read or write of anywhere in language or OS
    // workspace; this will catch anything we haven't explicitly implemented,
    // since BASIC isn't actually running on the emulated machine.
    for (uint16_t address = 0; address < 0x100; ++address) {
        set_abort_callback(address);
    }
    for (uint16_t address = 0x400; address < 0x800; ++address) {
        set_abort_callback(address);
    }
}

int main(int argc, char *argv[]) {
    init();
}

// vi: colorcolumn=80
