// TODO: CHECK ALL FILES TO MAKE SURE "LOCAL" FNS ARE STATIC
#include "emulation.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "driver.h"
#include "lib6502.h"
#include "roms.h"
#include "utils.h"

// TODO: I am quite inconsistent about mpu->foo vs just using these structure
// directly. I should be consistent, but when choosing how to be consistent,
// perhaps favour the shortest code?
// TODO: SOME/ALL OF THESE SHOULD MAYBE BE STATIC?
static M6502_Registers mpu_registers;
M6502_Memory mpu_memory;
static M6502_Callbacks mpu_callbacks;
static M6502 *mpu;

// M6502_run() never returns, so we use this jmp_buf to return control when
// the emulated machine is waiting for user input.
static jmp_buf mpu_env;

static int vdu_variables[257];

static enum {
    ms_running,
    ms_osword_input_line_pending,
    ms_osrdch_pending,
} mpu_state = ms_running;

// We copy transient bits of machine code to transient_code for execution; such
// code must not make a subroutine call to anything which could in turn overwrite
// transient_code, as the code following the JSR would then be overwritten when
// it returned.
static const int transient_code = 0x900;
// The code to invoke the service handler can't live at transient_code as it
// needs to JSR into the arbitrary ROM code, so it has its own space.
static const int service_code = 0xb00;

static void mpu_write_u16(uint16_t address, uint16_t data) {
    mpu_memory[address    ] = data & 0xff;
    mpu_memory[address + 1] = (data >> 8) & 0xff;
}

uint16_t mpu_read_u16(uint16_t address) {
    return (mpu_memory[address + 1] << 8) | mpu_memory[address];
}

static void mpu_clear_carry(M6502 *mpu) {
    mpu->registers->p &= ~(1<<0);
}

static void mpu_dump(void) {
    char buffer[64];
    M6502_dump(mpu, buffer);
    fprintf(stderr, "6502 state: %s\n", buffer);
}

// TODO: Rename to get rid of '2' and to show it doesn't actually *enter* BASIC, just returns address to call to do so - prepare_basic_entry()??
static uint16_t enter_basic2(void) {
    mpu_registers.a = 1; // language entry special value in A
    mpu_registers.x = 0;
    mpu_registers.y = 0;

    const uint16_t code_address = transient_code;
    uint8_t *p = &mpu_memory[code_address];
    *p++ = 0xa2; *p++ = bank_basic;        // LDX #bank_basic
    *p++ = 0x86; *p++ = 0xf4;              // STX &F4
    *p++ = 0x8e; *p++ = 0x30; *p++ = 0xfe; // STX &FE30
    *p++ = 0x4c; *p++ = 0x00; *p++ = 0x80; // JMP &8000 (language entry)

    return code_address;
}

NORETURN static void callback_abort(const char *type, uint16_t address, uint8_t data) {
    die("Error: Unexpected %s at address %04x, data %02x", type, address, data);
}

NORETURN static int callback_abort_read(M6502 *mpu, uint16_t address, uint8_t data) {
    // externalise() hasn't been called by lib6502 at this point so we can't
    // dump the registers.
    callback_abort("read", address, data);
}

NORETURN static int callback_abort_write(M6502 *mpu, uint16_t address, uint8_t data) {
    // externalise() hasn't been called by lib6502 at this point so we can't
    // dump the registers.
    callback_abort("write", address, data);
}

NORETURN static int callback_abort_call(M6502 *mpu, uint16_t address, uint8_t data) {
    mpu_dump();
    callback_abort("call", address, data);
}

static int callback_return_via_rts(M6502 *mpu) {
    uint16_t address = mpu_read_u16(0x101 + mpu->registers->s);
    mpu->registers->s += 2;
    address += 1;
    //fprintf(stderr, "SFTODOXXX %04x\n", address);
    return address;
}

static int callback_osrdch(M6502 *mpu, uint16_t address, uint8_t data) {
    //fprintf(stderr, "SFTODOSSA\n");
    mpu_state = ms_osrdch_pending;
    longjmp(mpu_env, 1);
}

static int callback_oswrch(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu->registers->a;
    pending_output_insert(c);
    return callback_return_via_rts(mpu);
}

static int callback_osnewl(M6502 *mpu, uint16_t address, uint8_t data) {
    pending_output_insert(0xa);
    pending_output_insert(0xd);
    return callback_return_via_rts(mpu);
}

static int callback_osasci(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu->registers->a;
    if (c == 0xd) {
        return callback_osnewl(mpu, address, data);
    } else {
        return callback_oswrch(mpu, address, data);
    }
}

static int callback_osbyte_return_x(M6502 *mpu, uint8_t x) {
    mpu->registers->x = x;
    return callback_return_via_rts(mpu);
}

static int callback_osbyte_return_u16(M6502 *mpu, uint16_t value) {
    mpu->registers->x = value & 0xff;
    mpu->registers->y = (value >> 8) & 0xff;
    return callback_return_via_rts(mpu);
}

static int callback_osbyte_read_vdu_variable(M6502 *mpu) {
    int i = mpu->registers->x;
    if (vdu_variables[i] == -1) {
        mpu_dump();
        die("Error: Unsupported VDU variable %d read", i);
    }
    if (vdu_variables[i + 1] == -1) {
        mpu_dump();
        die("Error: Unsupported VDU variable %d read", i + 1);
    }
    mpu->registers->x = vdu_variables[i];
    mpu->registers->y = vdu_variables[i + 1];
    return callback_return_via_rts(mpu);
}

// TODO: Not here (this has to follow std prototype), but get rid of pointless mpu argument on some functions?
static int callback_osbyte(M6502 *mpu, uint16_t address, uint8_t data) {
    switch (mpu->registers->a) {
        case 0x03: // select output device
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x0f: // flush buffers
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x7c: // clear Escape condition
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x7e: // acknowledge Escape condition
            return callback_osbyte_return_x(mpu, 0); // no Escape condition pending
        case 0x83: // read OSHWM
            return callback_osbyte_return_u16(mpu, page);
        case 0x84: // read HIMEM
            return callback_osbyte_return_u16(mpu, himem);
        case 0x86: // read text cursor position
            return callback_osbyte_return_u16(mpu, 0); // TODO: MAGIC CONST, HACK
        case 0x8a: // place character into buffer
            // TODO: We probably shouldn't be treating this is a no-op but let's hack
            return callback_return_via_rts(mpu); // treat as no-op
        case 0xa0:
            return callback_osbyte_read_vdu_variable(mpu);
        default:
            mpu_dump();
            die("Error: Unsupported OSBYTE");
    }
}

static int callback_oscli(M6502 *mpu, uint16_t address, uint8_t data) {
    uint16_t yx = (mpu_registers.y << 8) | mpu_registers.x;
    mpu_memory[0xf2] = mpu_registers.x;
    mpu_memory[0xf3] = mpu_registers.y;
    //fprintf(stderr, "SFTODOXCC %c%c%c\n", mpu_memory[yx], mpu_memory[yx+1], mpu_memory[yx+2]);

    // Because our ROMSEL implementation will treat it as an error to page in
    // an empty bank, the following code only works with ABE in banks 0 and 1.
    // This could be changed if necessary.
    assert(bank_editor_a == 0);
    assert(bank_editor_b == 1);
    mpu_registers.a = 4; // unrecognised * command
    mpu_registers.x = bank_editor_b; // first ROM bank to try
    mpu_registers.y = 0; // command tail offset

    // It's tempting to implement a "mini OS" in assembler which would replace
    // the following mixture of C and machine code, as well as other fragments
    // scattered around this file. However, I don't want to create a build
    // dependency on a 6502 assembler and hand-assembling is tedious and
    // error-prone, so this approach minimises the amount of hand-assembled
    // code.

    // Skip leading "*" on the command; this is essential to have it recognised
    // properly (as that's what the real OS does).
    while (mpu_memory[yx + mpu_registers.y] == '*') {
        ++mpu_registers.y;
        check(mpu_registers.y != 0, "Internal error: Too many *s on OSCLI"); // unlikely!
    }

    // This isn't case-insensitive and doesn't recognise abbreviations, but
    // in practice it's good enough.
    if (memcmp((char *) &mpu_memory[yx + mpu_registers.y], "BASIC", 5) == 0) {
        //fprintf(stderr, "SFTODO BASIC!\n");
        return enter_basic2();
    }

    const uint16_t code_address = service_code;
    uint8_t *p = &mpu_memory[code_address];
                                           // .loop
    *p++ = 0x86; *p++ = 0xf4;              // STX &F4
    *p++ = 0x8e; *p++ = 0x30; *p++ = 0xfe; // STX &FE30
    *p++ = 0x20; *p++ = 0x03; *p++ = 0x80; // JSR &8003 (service entry)
    *p++ = 0xa6; *p++ = 0xf4;              // LDX &F4
    *p++ = 0xca;                           // DEX
    *p++ = 0x10; *p++ = 256 - 13;          // BPL loop
    *p++ = 0xc9; *p++ = 0;                 // CMP #0
    *p++ = 0xd0; *p++ = 1;                 // BNE skip_rts
    *p++ = 0x60;                           // RTS
                                           // .skip_rts
    *p++ = 0x00;                           // BRK
    *p++ = 0xfe;                           // error code
    strcpy((char *) p, "Bad command");     // error string and terminator
    
    //fprintf(stderr, "SFTODO999\n");
    return code_address;
}

static int callback_osword_input_line(M6502 *mpu) {
    //fprintf(stderr, "SFTODOXA2\n");
    mpu_state = ms_osword_input_line_pending;
    longjmp(mpu_env, 1);
}

static int callback_osword_read_io_memory(M6502 *mpu) {
    // We do this access via dynamically generated code so we don't bypass any
    // lib6502 callbacks.
    uint16_t yx = (mpu->registers->y << 8) | mpu->registers->x;
    uint16_t source = mpu_read_u16(yx);
    uint16_t dest = yx + 4;
    const uint16_t code_address = transient_code;
    uint8_t *p = &mpu_memory[code_address];
    *p++ = 0xad; *p++ = source & 0xff; *p++ = (source >> 8) & 0xff; // LDA source
    *p++ = 0x8d; *p++ = dest & 0xff; *p++ = (dest >> 8) & 0xff;     // STA dest
    *p++ = 0x60;                                                    // RTS
    return code_address;
}

static int callback_osword(M6502 *mpu, uint16_t address, uint8_t data) {
    switch (mpu->registers->a) {
        case 0x00: // input line
            return callback_osword_input_line(mpu);
        case 0x05: // read I/O processor memory
            return callback_osword_read_io_memory(mpu);
        default:
            mpu_dump();
            die("Error: Unsupported OSWORD");
    }
}

static int callback_read_escape_flag(M6502 *mpu, uint16_t address, uint8_t data) {
    return 0; // Escape flag not set
}

static int callback_romsel_write(M6502 *mpu, uint16_t address, uint8_t data) {
    uint8_t *rom_start = &mpu_memory[0x8000];
    const size_t rom_size = 16 * 1024;
    switch (data) {
        case bank_editor_a:
            memcpy(rom_start, rom_editor_a, rom_size);
            break;
        case bank_editor_b:
            memcpy(rom_start, rom_editor_b, rom_size);
            break;
        case bank_basic:
            memcpy(rom_start, rom_basic, rom_size);
            break;
        default:
            die("Internal error: Invalid ROM bank %d selected", data);
            break;
    }
    return 0; // return value ignored
}

static int callback_irq(M6502 *mpu, uint16_t address, uint8_t data) {
    // The only possible cause of an interrupt on our emulated machine is a BRK
    // instruction.
    // TODO: Copy and paste of code from callback_return_via_rts() - not quite
    uint16_t error_string_ptr = mpu_read_u16(0x102 + mpu->registers->s);
    mpu->registers->s += 2; // not really necessary, as we're about to exit()
    uint16_t error_num_address = error_string_ptr - 1;
    fprintf(stderr, "Error: ");
    for (uint8_t c; (c = mpu->memory[error_string_ptr]) != '\0'; ++error_string_ptr) {
        fputc(c, stderr);
    }
    uint8_t error_num = mpu->memory[error_num_address];
    // TODO: We will need an ability to include a pseudo-line number if we're tokenising a BASIC program - but maybe not just here, maybe on other errors too (e.g. in die()?)
    fprintf(stderr, " (%d)\n", error_num);
    exit(EXIT_FAILURE);
}

static void callback_poll(M6502 *mpu) {
}

static void set_abort_callback(uint16_t address) {
    M6502_setCallback(mpu, read,  address, callback_abort_read);
    // TODO: Get rid of write callback permanently?
    M6502_setCallback(mpu, write, address, callback_abort_write);
}

static void mpu_run() {
    if (setjmp(mpu_env) == 0) {
        mpu_state = ms_running;
        M6502_run(mpu, callback_poll); // returns only via longjmp(mpu_env)
    }
}

void emulation_init(void) {
    mpu = check_alloc(M6502_new(&mpu_registers, mpu_memory, &mpu_callbacks));
    M6502_reset(mpu);
    
    // Install handlers to abort on read or write of anywhere in OS workspace
    // we haven't explicitly allowed; this makes it more obvious if the OS
    // emulation needs to be extended. Addresses 0x90-0xaf are used, but they
    // don't contain OS state we need to emulate so this loop excludes them.
    for (uint16_t address = 0xb0; address < 0x100; ++address) {
        switch (address) {
            case 0xf2: // OS text pointer
            case 0xf3: // OS text pointer
            case 0xf4: // ROMSEL copy
            case 0xfd: // error pointer
            case 0xfe: // error pointer
                // Supported as far as necessary, don't install a handler.
                break;

            default:
                set_abort_callback(address);
                break;
        }
    }

    // Trap access to unimplemented OS vectors.
    for (uint16_t address = 0x200; address < 0x236; ++address) {
        switch (address) {
            case 0x202: // BRKV
            case 0x203: // BRKV
            case 0x20e: // WRCHV
            case 0x20f: // WRCHV
                // Supported as far as necessary, don't install a handler.
                break;
            default:
                set_abort_callback(address);
                break;
        }
    }

    // Install handlers for OS entry points, using a default for unimplemented
    // ones.
    for (uint16_t address = 0xc000; address != 0; ++address) {
        M6502_setCallback(mpu, call, address, callback_abort_call);
    }
    M6502_setCallback(mpu, call, 0xffe0, callback_osrdch);
    M6502_setCallback(mpu, call, 0xffe3, callback_osasci);
    M6502_setCallback(mpu, call, 0xffe7, callback_osnewl);
    M6502_setCallback(mpu, call, 0xffee, callback_oswrch);
    M6502_setCallback(mpu, call, 0xfff1, callback_osword);
    M6502_setCallback(mpu, call, 0xfff4, callback_osbyte);
    M6502_setCallback(mpu, call, 0xfff7, callback_oscli);

    // Install fake OS vectors. Because of the way our implementation works,
    // these vectors actually point to the official entry points.
    mpu_write_u16(0x20e, 0xffee); // SFTODO: MAGIC CONSTANT IN A COUPLE OF PLACES

    // Since we don't have an actual Escape handler, just ensure any read from
    // &ff always returns 0.
    M6502_setCallback(mpu, read, 0xff, callback_read_escape_flag);

    // Install handler for hardware ROM paging emulation.
    M6502_setCallback(mpu, write, 0xfe30, callback_romsel_write);

    // Install interrupt handler so we can catch BRK.
    M6502_setVector(mpu, IRQ, 0xf000); // SFTODO: Magic constant
    M6502_setCallback(mpu, call, 0xf000, callback_irq);

    // Set up VDU variables.
    for (int i = 0; i < 256; ++i) {
        vdu_variables[i] = -1;
    }
    vdu_variables[0x55] = 7; // screen mode
    vdu_variables[0x56] = 4; // memory map type: 1K mode

    mpu_registers.s = 0xff;
    mpu_registers.pc = enter_basic2(); // SFTODO: RENAME TO INDICATE DOESN'T ENTER DIRECTLY
    mpu_run();
}

// TODO: MOVE
// TODO: RENAME
void execute_osrdch(const char *s) {
    assert(strlen(s) == 1);
    check(mpu_state == ms_osrdch_pending, "Internal error: Emulated machine isn't waiting for OSRDCH");
    char c = s[0];
    mpu->registers->a = c;
    mpu_clear_carry(mpu); // no error
    // TODO: Following code fragment may be common to OSWORD 0 and can be factored out
    mpu->registers->pc = callback_return_via_rts(mpu);
    //fprintf(stderr, "SFTODOZX022 %d\n", c);
    mpu_run();
    //fprintf(stderr, "SFTODOZXA22\n");
} 

// TODO: MOVE
void execute_input_line(const char *line) {
    check(mpu_state == ms_osword_input_line_pending, "Internal error: Emulated machine isn't waiting for OSWORD 0");
    //fprintf(stderr, "SFTODOXA\n");
    // TODO: We must respect the maximum line length
    uint16_t yx = (mpu->registers->y << 8) | mpu->registers->x;
    uint16_t buffer = mpu_read_u16(yx);
    uint8_t buffer_size = mpu_memory[yx + 2];
    size_t pending_length = strlen(line);
    check(pending_length < buffer_size, "Error: Line too long"); // TODO: PROPER ERROR ETC - BUT WE DO WANT TO TREAT THIS AS AN ERROR, NOT TRUNCATE - WE MAY ULTIMATELY WANT TO BE GIVING A LINE NUMBER FROM INPUT IF WE'RE TOKENISING BASIC VIA THIS
    memcpy(&mpu_memory[buffer], line, pending_length);

    // OSWORD 0 would echo the typed characters and move to a new line, so do
    // the same with our pending output.
    for (int i = 0; i < pending_length; ++i) {
        pending_output_insert(line[i]);
    }
    pending_output_insert(0xa); pending_output_insert(0xd);

    mpu_memory[buffer + pending_length] = 0xd;
    mpu->registers->y = pending_length; // TODO HACK
    mpu_clear_carry(mpu); // input not terminated by Escape
    mpu->registers->pc = callback_return_via_rts(mpu);
    //fprintf(stderr, "SFTODOZX0\n");
    mpu_run();
    //fprintf(stderr, "SFTODOZXA\n");
}
