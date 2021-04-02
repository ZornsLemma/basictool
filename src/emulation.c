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

static M6502_Registers mpu_registers;
M6502_Memory mpu_memory;
static M6502_Callbacks mpu_callbacks;
static M6502 *mpu;

// M6502_run() never returns, so we use this jmp_buf to return control when
// the emulated machine is waiting for user input.
static jmp_buf mpu_env;

// We want vdu_variables[256] to be a valid reference, because our VDU variable
// implementation will potentially access it and we don't want to invoke
// undefined behaviour. (In practice this won't happen at present.)
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

enum {
    os_text_pointer = 0xf2,
    romsel_copy = 0xf4,
    brkv = 0x202,
    wrchv = 0x20e,
    fake_irq_handler = 0xf000,
    oswrch = 0xffee
};

static void mpu_write_u16(uint16_t address, uint16_t data) {
    mpu_memory[address    ] = data & 0xff;
    mpu_memory[address + 1] = (data >> 8) & 0xff;
}

uint16_t mpu_read_u16(uint16_t address) {
    return (mpu_memory[address + 1] << 8) | mpu_memory[address];
}

static void mpu_clear_carry() {
    mpu_registers.p &= ~(1<<0);
}

static void mpu_dump(void) {
    char buffer[64];
    M6502_dump(mpu, buffer);
    fprintf(stderr, "6502 state: %s\n", buffer);
}

// Prepare to enter BASIC, returning the address of code which will actually
// enter it.
static uint16_t enter_basic(void) {
    mpu_registers.a = 1; // language entry special value in A
    mpu_registers.x = 0;
    mpu_registers.y = 0;

    const uint16_t code_address = transient_code;
    uint8_t *p = &mpu_memory[code_address];
    *p++ = 0xa2; *p++ = bank_basic;        // LDX #bank_basic
    *p++ = 0x86; *p++ = romsel_copy;       // STX romsel_copy
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

static int callback_return_via_rts() {
    uint16_t address = mpu_read_u16(0x101 + mpu_registers.s);
    mpu_registers.s += 2;
    address += 1;
    return address;
}

static int callback_osrdch(M6502 *mpu, uint16_t address, uint8_t data) {
    mpu_state = ms_osrdch_pending;
    longjmp(mpu_env, 1);
}

static int callback_oswrch(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu_registers.a;
    driver_oswrch(c);
    return callback_return_via_rts(mpu);
}

static int callback_osnewl(M6502 *mpu, uint16_t address, uint8_t data) {
    driver_oswrch(0xa);
    driver_oswrch(0xd);
    return callback_return_via_rts(mpu);
}

static int callback_osasci(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu_registers.a;
    if (c == 0xd) {
        return callback_osnewl(mpu, address, data);
    } else {
        return callback_oswrch(mpu, address, data);
    }
}

static int callback_osbyte_return_x(uint8_t x) {
    mpu_registers.x = x;
    return callback_return_via_rts(mpu);
}

static int callback_osbyte_return_u16(uint16_t value) {
    mpu_registers.x = value & 0xff;
    mpu_registers.y = (value >> 8) & 0xff;
    return callback_return_via_rts(mpu);
}

static int callback_osbyte_read_vdu_variable(void) {
    int i = mpu_registers.x;
    if (vdu_variables[i] == -1) {
        mpu_dump();
        die("Error: Unsupported VDU variable %d read", i);
    }
    if (vdu_variables[i + 1] == -1) {
        mpu_dump();
        die("Error: Unsupported VDU variable %d read", i + 1);
    }
    mpu_registers.x = vdu_variables[i];
    mpu_registers.y = vdu_variables[i + 1];
    return callback_return_via_rts(mpu);
}

static int callback_osbyte(M6502 *mpu, uint16_t address, uint8_t data) {
    switch (mpu_registers.a) {
        case 0x03: // select output device
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x0f: // flush buffers
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x7c: // clear Escape condition
            return callback_return_via_rts(mpu); // treat as no-op
        case 0x7e: // acknowledge Escape condition
            return callback_osbyte_return_x(0); // no Escape condition pending
        case 0x83: // read OSHWM
            return callback_osbyte_return_u16(page);
        case 0x84: // read HIMEM
            return callback_osbyte_return_u16(himem);
        case 0x86: // read text cursor position
            // We just return with X=Y=0; this is good enough in practice.
            return callback_osbyte_return_u16(0);
        case 0x8a: // place character into buffer
            // ABE uses this to type "OLD<return>" when re-entering BASIC. It
            // might be nice to emulate this properly, but it also seems silly
            // to complicate the I/O emulation further when we can simply
            // do this explicitly.
            return callback_return_via_rts(); // treat as no-op
        case 0xa0:
            return callback_osbyte_read_vdu_variable();
        default:
            mpu_dump();
            die("Error: Unsupported OSBYTE");
    }
}

static int callback_oscli(M6502 *mpu, uint16_t address, uint8_t data) {
    uint16_t yx = (mpu_registers.y << 8) | mpu_registers.x;
    mpu_memory[os_text_pointer    ] = mpu_registers.x;
    mpu_memory[os_text_pointer + 1] = mpu_registers.y;

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
        return enter_basic();
    }

    const uint16_t code_address = service_code;
    uint8_t *p = &mpu_memory[code_address];
                                           // .loop
    *p++ = 0x86; *p++ = romsel_copy;       // STX romsel_copy
    *p++ = 0x8e; *p++ = 0x30; *p++ = 0xfe; // STX &FE30
    *p++ = 0x20; *p++ = 0x03; *p++ = 0x80; // JSR &8003 (service entry)
    *p++ = 0xa6; *p++ = romsel_copy;       // LDX romsel_copy
    *p++ = 0xca;                           // DEX
    *p++ = 0x10; *p++ = 256 - 13;          // BPL loop
    *p++ = 0xc9; *p++ = 0;                 // CMP #0
    *p++ = 0xd0; *p++ = 1;                 // BNE skip_rts
    *p++ = 0x60;                           // RTS
                                           // .skip_rts
    *p++ = 0x00;                           // BRK
    *p++ = 0xfe;                           // error code
    strcpy((char *) p, "Bad command");     // error string and terminator
    
    return code_address;
}

static int callback_osword_input_line(void) {
    mpu_state = ms_osword_input_line_pending;
    longjmp(mpu_env, 1);
}

static int callback_osword_read_io_memory(void) {
    // We do this access via dynamically generated code so we don't bypass any
    // lib6502 callbacks.
    uint16_t yx = (mpu_registers.y << 8) | mpu_registers.x;
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
    switch (mpu_registers.a) {
        case 0x00: // input line
            return callback_osword_input_line();
        case 0x05: // read I/O processor memory
            return callback_osword_read_io_memory();
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
    uint16_t error_string_ptr = mpu_read_u16(0x102 + mpu_registers.s);
    mpu_registers.s += 2; // not really necessary, as we're about to exit()
    uint16_t error_num_address = error_string_ptr - 1;
    print_error_filename_prefix();
    fprintf(stderr, "Error: ");
    for (uint8_t c; (c = mpu_memory[error_string_ptr]) != '\0'; ++error_string_ptr) {
        fputc(c, stderr);
    }
    uint8_t error_num = mpu_memory[error_num_address];
    // TODO: We will need an ability to include a pseudo-line number if we're tokenising a BASIC program - but maybe not just here, maybe on other errors too (e.g. in die()?)
    fprintf(stderr, " (%d)\n", error_num);
    exit(EXIT_FAILURE);
}

static void callback_poll(M6502 *mpu) {
}

static void set_abort_callback(uint16_t address) {
    M6502_setCallback(mpu, read,  address, callback_abort_read);
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
            case os_text_pointer:
            case os_text_pointer + 1:
            case romsel_copy:
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
            case brkv:
            case brkv + 1:
            case wrchv:
            case wrchv + 1:
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
    M6502_setCallback(mpu, call, oswrch, callback_oswrch);
    M6502_setCallback(mpu, call, 0xfff1, callback_osword);
    M6502_setCallback(mpu, call, 0xfff4, callback_osbyte);
    M6502_setCallback(mpu, call, 0xfff7, callback_oscli);

    // Install fake OS vectors. Because of the way our implementation works,
    // these vectors actually point to the official entry points.
    mpu_write_u16(wrchv, oswrch);

    // Since we don't have an actual Escape handler, just ensure any read from
    // &ff always returns 0.
    M6502_setCallback(mpu, read, 0xff, callback_read_escape_flag);

    // Install handler for hardware ROM paging emulation.
    M6502_setCallback(mpu, write, 0xfe30, callback_romsel_write);

    // Install interrupt handler so we can catch BRK.
    M6502_setVector(mpu, IRQ, fake_irq_handler);
    M6502_setCallback(mpu, call, fake_irq_handler, callback_irq);

    // Set up VDU variables.
    for (int i = 0; i < 256; ++i) {
        vdu_variables[i] = -1;
    }
    vdu_variables[0x55] = 7; // screen mode
    vdu_variables[0x56] = 4; // memory map type: 1K mode

    mpu_registers.s = 0xff;
    mpu_registers.pc = enter_basic();
    mpu_run();
}

void execute_osrdch(const char *s) {
    assert(s != 0);
    // TODO: We could in principle handle a multiple character string by
    // returning the values automatically over multiple OSRDCH calls, but we
    // don't need this yet.
    check(strlen(s) == 1,
          "Internal error: Attempt to return multiple characters from OSRDCH");
    check(mpu_state == ms_osrdch_pending,
          "Internal error: Emulated machine isn't waiting for OSRDCH");
    mpu_registers.a = s[0];
    mpu_clear_carry(mpu); // no error
    mpu_registers.pc = callback_return_via_rts(mpu);
    mpu_run();
} 

void execute_input_line(const char *line) {
    assert(line != 0);
    check(mpu_state == ms_osword_input_line_pending,
          "Internal error: Emulated machine isn't waiting for OSWORD 0");
    uint16_t yx = (mpu_registers.y << 8) | mpu_registers.x;
    uint16_t buffer = mpu_read_u16(yx);
    uint8_t buffer_size = mpu_memory[yx + 2];
    size_t pending_length = strlen(line);
    check(pending_length < buffer_size, "Error: Line too long");
    memcpy(&mpu_memory[buffer], line, pending_length);

    // OSWORD 0 would echo the typed characters and move to a new line, so do
    // the same.
    for (int i = 0; i < pending_length; ++i) {
        driver_oswrch(line[i]);
    }
    driver_oswrch(0xa); driver_oswrch(0xd);

    mpu_memory[buffer + pending_length] = 0xd;
    mpu_registers.y = pending_length;
    mpu_clear_carry(mpu); // input not terminated by Escape
    mpu_registers.pc = callback_return_via_rts(mpu);
    mpu_run();
}

// TODO: Bit OTT but be good to be consistent about full stop vs no full stop at end of all error messages, or at least *think* about whether we want one or not on each case rather than it being a bit ad-hoc

// TODO: gcc error messages are "all lower case", e.g. "foo.c:42: error: your code sucks", since I'm copying gcc format for line number-related error messages, maybe I should use this style more/everywhere?

// vi: colorcolumn=80
