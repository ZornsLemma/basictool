// TODO: This file is a temporary collection of stuff as I refactor, there shouldn't ultimately be a file called other.c!

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "data.h"
#include "lib6502.h"
#include "utils.h"

// TODO: I am quite inconsistent about mpu->foo vs just using these structure
// directly. I should be consistent, but when choosing how to be consistent,
// perhaps favour the shortest code?
M6502_Registers mpu_registers;
M6502_Memory mpu_memory;
M6502_Callbacks mpu_callbacks;
M6502 *mpu;

const char *pending_osword_input_line = 0;

enum {
    os_discard,
    os_list_discard_command,
    os_list,
    os_pack_discard_concatenate,
    os_pack_discard_blank,
    os_pack
} output_state;
FILE *output_state_file = 0; // TODO: RENAME? REMOVE "STATE"?

char *pending_output = 0;
size_t pending_output_length = 0;
size_t pending_output_cursor_x = 0;
size_t pending_output_buffer_size = 0;

// TODO: Ideally we would include this in *any* error message if it's not -1,
// but it may be OK if it's a lot cleaner/easier to just do it in carefully
// selected places.
int error_line_number = -1;

// TODO: This probably needs expanding etc, I'm hacking right now
// TODO: Should probably not check this via assert(), it *shouldn't* go wrong
// if there are no program bugs, but it is quite likely some strange situations
// can occur if given invalid user input and ABE or BASIC does something I am not expecting in response.
enum {
    SFTODOIDLE, // SFTODO: NOT "IDLE", RENAME THIS
    osword_input_line_pending,
    osrdch_pending,
} state = SFTODOIDLE; // SFTODO: 'state' IS TOO SHORT A NAME

// M6502_run() never returns, so we use this jmp_buf to return control when a
// task has finished executing on the emulated CPU.
jmp_buf mpu_env;

extern const char *program_name; // TODO!

#define ROM_SIZE (16 * 1024)
// TODO: Support for HIBASIC might be nice (only for tokenising/detokenising;
// ABE runs at &8000 so probably can't work with HIBASIC-sized programs), but
// let's not worry about that yet.

#define BASIC_LOMEM (0x0)
#define BASIC_HEAP (0x2)
#define BASIC_TOP (0x12)
#define BASIC_PAGE (0x18) // Just the high byte
#define BASIC_HIMEM (0x6)

const uint16_t page = 0xe00;
const uint16_t himem = 0x8000;

int vdu_variables[257];

extern const char *filenames[2]; // TODO!

const char *osrdch_queue = 0;

// TODO: I should probably extend check to be printf-like and make all callers use this where helpful
void check(bool b, const char *s) {
    if (!b) {
        die("%s", s);
    }
}

void *check_alloc(void *p) {
    check(p != 0, "Error: Unable to allocate memory");
    return p;
}

// TODO: This should probably be printf-like, but check callers - they may not need it
void die_help(const char *message) {
    die("%s\nTry '%s --help' for more information.", message, program_name);
}

// TODO: For stdout to be useful, I need to be sure all verbose output etc is written to stderr - maybe not, it depends how you view the verbose output. A user might want to do "basictool input.txt --pack -vv output.tok > pack-output.txt"; if we output to stderr this redirection becomes fiddlier.
FILE *fopen_wrapper(const char *pathname, const char *mode) {
    if (pathname == 0) {
        assert(mode != 0);
        if (strcmp(mode, "rb") == 0) {
            return stdin;
        } else if (strcmp(mode, "wb") == 0) {
            return stdout;
        } else {
            die("Internal error: Invalid mode passed to fopen_wrapper()");
        }
    } else {
        return fopen(pathname, mode);
    }
}

void finished(void);

void mpu_write_u16(uint16_t address, uint16_t data) {
    mpu_memory[address    ] = data & 0xff;
    mpu_memory[address + 1] = (data >> 8) & 0xff;
}

uint16_t mpu_read_u16(uint16_t address) {
    return (mpu_memory[address + 1] << 8) | mpu_memory[address];
}

void mpu_clear_carry(M6502 *mpu) {
    mpu->registers->p &= ~(1<<0);
}

void mpu_dump(void) {
    char buffer[64];
    M6502_dump(mpu, buffer);
    fprintf(stderr, "6502 state: %s\n", buffer);
}

// TODO: COPY AND PASTE OF enter_basic()
uint16_t enter_basic2(void) {
    mpu_registers.a = 1; // language entry special value in A
    mpu_registers.x = 0;
    mpu_registers.y = 0;

    const uint16_t code_address = 0x900;
    uint8_t *p = &mpu_memory[code_address];
    *p++ = 0xa2; *p++ = 12;                // LDX #12 TODO: MAGIC CONSTANT
    *p++ = 0x86; *p++ = 0xf4;              // STX &F4
    *p++ = 0x8e; *p++ = 0x30; *p++ = 0xfe; // STX &FE30
    *p++ = 0x4c; *p++ = 0x00; *p++ = 0x80; // JMP &8000 (language entry)

    return code_address;
}

NORETURN void callback_abort(const char *type, uint16_t address, uint8_t data) {
    die("Error: Unexpected %s at address %04x, data %02x", type, address, data);
}

NORETURN int callback_abort_read(M6502 *mpu, uint16_t address, uint8_t data) {
    // externalise() hasn't been called by lib6502 at this point so we can't
    // dump the registers.
    callback_abort("read", address, data);
}

NORETURN int callback_abort_write(M6502 *mpu, uint16_t address, uint8_t data) {
    // externalise() hasn't been called by lib6502 at this point so we can't
    // dump the registers.
    callback_abort("write", address, data);
}

NORETURN int callback_abort_call(M6502 *mpu, uint16_t address, uint8_t data) {
    mpu_dump();
    callback_abort("call", address, data);
}

int callback_return_via_rts(M6502 *mpu) {
    uint8_t low  = mpu->memory[0x101 + mpu->registers->s];
    uint8_t high = mpu->memory[0x102 + mpu->registers->s];
    mpu->registers->s += 2;
    uint16_t address = (high << 8) | low;
    address += 1;
    //fprintf(stderr, "SFTODOXXX %04x\n", address);
    return address;
}

int callback_osrdch(M6502 *mpu, uint16_t address, uint8_t data) {
    //fprintf(stderr, "SFTODOSSA\n");
    state = osrdch_pending;
    longjmp(mpu_env, 1);
}

// TODO: Output should ultimately be gated via a -v option, perhaps with some
// sort of (optional but default) filtering to tidy it up

int max(int lhs, int rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

bool is_in_pending_output(const char *s) {
    return strstr(pending_output, s) != 0;
}

char *make_printable(const char *s) {
    return (char *) s; // TODO: total hack, implement properly - this should escape control characters, e.g. as \xnn - it can just malloc space for the result - or maybe we could take a non-const char argument and just replace with spaces, that might actually be better
}

void check_pending_output(const char *s) {
    if (is_in_pending_output(s)) {
        return;
    }
    // TODO: Perhaps suggest use of the debug output help option in this message? (On a new line after the existing message)
    die("Error: Expected to see output containing '%s', got '%s'", s,
        make_printable(pending_output));
}


void complete_output_line_handler(const char *line) {
#if 0 // TODO
    if (true) { // SFTODO CONFIG FOR "SHOW ALL OUTPUT"
        fprintf(stderr, "SFTODOHQQ:%d:%s\n", output_state, line);
    }
#endif
    switch (output_state) {
        case os_discard:
            break;

        case os_list_discard_command:
            check_pending_output(">LIST");
            output_state = os_list;
            break;

        case os_list:
            assert(output_state_file != 0);
            fprintf(output_state_file, "%s\n", line);
            break;

        case os_pack_discard_concatenate:
            check_pending_output("Concatenate?");
            output_state = os_pack_discard_blank;
            break;

        case os_pack_discard_blank:
            check(*pending_output == '\0', "Error: Expected to see a blank line"); // TODO: SHOULD SAY GOT XXX - NEED CHECK TO BE PRINTF-IFIED
            output_state = os_pack;
            break;

        case os_pack:
            // TODO: This needs to respect verbosity
            if (config.verbose >= 1) {
                bool is_bytes_saved = is_in_pending_output("Bytes saved");
                if (is_bytes_saved || (config.verbose >= 2)) {
                    // TODO: We could maybe force alignment of the columns
                    // in the verbose>=2 output
                    // TODO: Should this go to stderr or stdout?
                    // TODO: Sanitise output? It should be fairly ASCII tho...
                    fprintf(stderr, "%s\n", line);
                }
                if (is_bytes_saved) {
                    output_state = os_discard;
                }
            }
            break;

        default:
            assert(false);
            break;
    }
}

void pending_output_insert(uint8_t data) {
    // We just discard NULs in the output; they aren't important for anything
    // we are emulating here.
    if (data == '\0') {
        return;
    }

    // SFTODO EXPERIMENTAL HACK
    if (data == 13) {
        pending_output_cursor_x = 0;
        return;
    }
    if (data == 10) {
        complete_output_line_handler(pending_output);
        pending_output_length = pending_output_cursor_x = 0;
        pending_output[0] = '\0';
        return;
    }

    if ((pending_output_cursor_x + 2) > pending_output_buffer_size) {
        if (pending_output_buffer_size == 0) {
            pending_output_buffer_size = 4; // TODO: make 64 or 128 or something
        } else {
            pending_output_buffer_size *= 2;
        }
        pending_output = check_alloc(realloc(pending_output, pending_output_buffer_size));
    }

    pending_output[pending_output_cursor_x] = data;
    pending_output[max(pending_output_cursor_x, pending_output_length) + 1] = '\0';
    pending_output_cursor_x += 1;
    pending_output_length = max(pending_output_cursor_x, pending_output_length);
    assert(strlen(pending_output) == pending_output_length);
}

int callback_oswrch(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu->registers->a;
    pending_output_insert(c);
#if 0 // TODO
    if (c == 0xa) {
        putchar('\n');
    } else if ((c >= ' ') && (c <= '~')) {
        putchar(c);
        //putchar('\n'); // SFTODO TEMP HACK
    }
#endif
    return callback_return_via_rts(mpu);
}

int callback_osnewl(M6502 *mpu, uint16_t address, uint8_t data) {
    pending_output_insert(0xa);
    pending_output_insert(0xd);
#if 0 // TODO
    putchar('\n');
#endif
    return callback_return_via_rts(mpu);
}

int callback_osasci(M6502 *mpu, uint16_t address, uint8_t data) {
    int c = mpu->registers->a;
    if (c == 0xd) {
        return callback_osnewl(mpu, address, data);
    } else {
        return callback_oswrch(mpu, address, data);
    }
}

int callback_osbyte_return_x(M6502 *mpu, uint8_t x) {
    mpu->registers->x = x;
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
int callback_osbyte(M6502 *mpu, uint16_t address, uint8_t data) {
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
            return callback_osbyte_return_u16(mpu, 0x8000); // TODO: MAGIC CONST
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

int callback_oscli(M6502 *mpu, uint16_t address, uint8_t data) {
    uint16_t yx = (mpu_registers.y << 8) | mpu_registers.x;
    mpu_memory[0xf2] = mpu_registers.x;
    mpu_memory[0xf3] = mpu_registers.y;
    //fprintf(stderr, "SFTODOXCC %c%c%c\n", mpu_memory[yx], mpu_memory[yx+1], mpu_memory[yx+2]);

    mpu_registers.a = 4; // unrecognised * command
    mpu_registers.x = 1; // current ROM bank TODO: MAGIC HACK, WE KNOW ABE IS BANKS 0 AND 1 AND THEY ARE THE ONLY BANKS WE NEED TO PASS SERVICE CALLS TO - IDEALLy WE'D RUN OVER ALL ROMS, THO BASIC HAS NO SERVICE ENTRY OF COURSE
    mpu_registers.y = 0; // command tail offset

    // TODO: It would be possible to write all this in assembler and have it
    // pre-built into a "mini OS" which we load at 0xc000 or 0xf000 or something.
    // However, we'd then need an assembler as part of the build process. We
    // could hand-assemble it like this, but do it once on startup, but then
    // we'd have to write more code (e.g. this "*" skipped loop) in hand-
    // assembled code, which would be painful.

    // Skip leading "*" on the command; this is essential to have it recognised
    // properly (as that's what the real OS does).
    while (mpu_memory[yx + mpu_registers.y] == '*') {
        ++mpu_registers.y;
        check(mpu_registers.y <= 255, "Too many * on OSCLI"); // unlikely!
    }

    // This isn't case-insensitive and doesn't recognise abbreviations, but
    // in practice it's good enough.
    if (memcmp(&mpu_memory[yx + mpu_registers.y], "BASIC", 5) == 0) {
        //fprintf(stderr, "SFTODO BASIC!\n");
        return enter_basic2();
    }

    const uint16_t code_address = 0xb00; // TODO: HACK - OTHER BITS OF HACKERY CAN OVERWRITE THIS WHILE WE'RE PART WAY THROUGH EXECUTING *BUTIL IF WE USE 0x900 - NO, THAT DOESN'T HELP, MAYBE THIS WOULD BE FINE, BUT LET'S LEAVE IT AT B00 FOR NOW
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
    strcpy(p, "Bad command");              // error string and terminator
    
    //fprintf(stderr, "SFTODO999\n");
    return code_address;
}

int callback_osword_input_line(M6502 *mpu) {
    //fprintf(stderr, "SFTODOXA2\n");
    state = osword_input_line_pending;
    longjmp(mpu_env, 1);
}


int callback_osword_read_io_memory(M6502 *mpu) {
    // So we don't bypass any lib6502 callbacks, we do this access via a
    // dynamically generated code stub.
    const uint16_t code_address = 0x900;
    uint8_t *p = &mpu_memory[code_address];
    uint16_t yx = (mpu->registers->y << 8) | mpu->registers->x;
    uint16_t source = mpu_read_u16(yx);
    uint16_t dest = yx + 4;
    *p++ = 0xad; *p++ = source & 0xff; *p++ = (source >> 8) & 0xff; // LDA source
    *p++ = 0x8d; *p++ = dest & 0xff; *p++ = (dest >> 8) & 0xff;     // STA dest
    *p++ = 0x60;                                                    // RTS
    return code_address;
}

int callback_osword(M6502 *mpu, uint16_t address, uint8_t data) {
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

int callback_read_escape_flag(M6502 *mpu, uint16_t address, uint8_t data) {
    return 0; // Escape flag not set
}

int callback_romsel_write(M6502 *mpu, uint16_t address, uint8_t data) {
    switch (data) {
        // TODO: The bank numbers should be named constants
        case 0:
            memcpy(&mpu_memory[0x8000], rom_editor_a, ROM_SIZE);
            break;
        case 1:
            memcpy(&mpu_memory[0x8000], rom_editor_b, ROM_SIZE);
            break;
        case 12: // same bank as on Master 128, but not really important
            memcpy(&mpu_memory[0x8000], rom_basic, ROM_SIZE);
            break;
        default:
            check(false, "Invalid ROM bank selected");
            break;
    }
    return 0; // return value ignored
}

int callback_irq(M6502 *mpu, uint16_t address, uint8_t data) {
    // The only possible cause of an interrupt on our emulated machine is a BRK
    // instruction.
    // TODO: Copy and paste of code from callback_return_via_rts() - not quite
    uint8_t low  = mpu->memory[0x102 + mpu->registers->s];
    uint8_t high = mpu->memory[0x103 + mpu->registers->s];
    mpu->registers->s += 2; // TODO not necessary, we won't return
    uint16_t error_string_ptr = (high << 8) | low;
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

void callback_poll(M6502 *mpu) {
}

void set_abort_callback(uint16_t address) {
    M6502_setCallback(mpu, read,  address, callback_abort_read);
    // TODO: Get rid of write callback permanently?
    //M6502_setCallback(mpu, write, address, callback_abort_write);
}

void init(void) {
    mpu = check_alloc(M6502_new(&mpu_registers, mpu_memory, &mpu_callbacks));
    M6502_reset(mpu);
    
#if 0 // TODO: Hack to switch between ABE and BASIC, should be runtime!
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
#endif
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
    M6502_setCallback(mpu, call, 0xfff1, callback_osword);
    M6502_setCallback(mpu, call, 0xfff4, callback_osbyte);
    M6502_setCallback(mpu, call, 0xfff7, callback_oscli);

    // Install fake OS vectors. Because of the way our implementation works,
    // these vectors actually point to the official entry points.
    mpu_write_u16(0x20e, 0xffee);

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

    output_state = os_discard; // TODO: MOVE ELSEWHERE?
}

// Read a file into a malloc()-ed block of memory. The pointer to the
// malloc()-ed block is returned and *length is set to the length.
char *load_binary(const char *filename, size_t *length) {
    assert(length != 0);
    FILE *file = fopen_wrapper(filename, "rb");
    check(file != 0, "Can't open input");
    // Since we're dealing with BASIC programs on a 32K-ish machine, we don't
    // need to handle arbitrarily large files.
    const int max_size = 64 * 1024;
    char *data = check_alloc(malloc(max_size));
    *length = fread(data, 1, max_size, file);
    check(!ferror(file), "Error reading input");
    check(feof(file), "Input is too large");
    check(fclose(file) == 0, "Error closing input");
    // We allocate an extra byte so we can easily guarantee that the last line
    // ends with a line terminator when reading non-tokenised input.
    return check_alloc(realloc(data, (*length) + 1));
}

// TODO: MOVE
// TODO: RENAME
void execute_osrdch(const char *s) {
    assert(strlen(s) == 1);
    assert(state == osrdch_pending);
    char c = s[0];
    mpu->registers->a = c;
    mpu_clear_carry(mpu); // no error
    // TODO: Following code fragment may be common to OSWORD 0 and can be factored out
    mpu->registers->pc = callback_return_via_rts(mpu);
    //fprintf(stderr, "SFTODOZX022 %d\n", c);
    // SFTODO: WE SHOULD PROBABLY ALWAYS SET STATE TO SOMETHING WHEN WE DO M6502_RUN
    if (setjmp(mpu_env) == 0) {
        //fprintf(stderr, "SFTODORUN\n");
        state = SFTODOIDLE;
        M6502_run(mpu, callback_poll); // never returns
    }
    //fprintf(stderr, "SFTODOZXA22\n");
} 

// TODO: MOVE
void execute_input_line(const char *line) {
    assert(state == osword_input_line_pending);
    //fprintf(stderr, "SFTODOXA\n");
    // TODO: We must respect the maximum line length
    uint16_t yx = (mpu->registers->y << 8) | mpu->registers->x;
    uint16_t buffer = mpu_read_u16(yx);
    uint8_t buffer_size = mpu_memory[yx + 2];
    size_t pending_length = strlen(line);
    check(pending_length < buffer_size, "Line too long"); // TODO: PROPER ERROR ETC - BUT WE DO WANT TO TREAT THIS AS AN ERROR, NOT TRUNCATE - WE MAY ULTIMATELY WANT TO BE GIVING A LINE NUMBER FROM INPUT IF WE'RE TOKENISING BASIC VIA THIS
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
    if (setjmp(mpu_env) == 0) {
        state = SFTODOIDLE;
        M6502_run(mpu, callback_poll); // never returns
    }
    //fprintf(stderr, "SFTODOZXA\n");
}

// TODO: COMMENT
// TODO: Review this later, I think there are no missing corner cases (bearing in mind we deliberately put a CR at the end of the input to catch unterminated last lines) but a fresh look would be good
char *get_line(char **data_ptr, size_t *length_ptr) {
    assert(data_ptr != 0);
    assert(length_ptr != 0);
    char *data = *data_ptr;
    size_t length = *length_ptr;

    if (length == 0) {
        return 0;
    }

    // Find the end of the line.
    char *eol = data;
    while ((*eol != 0x0d) && (*eol != 0x0a)) {
        ++eol; --length;
    }
    assert(length > 0);
    char terminator = *eol;
    *eol = '\0'; --length;

    // If there is a next character and it's the opposite terminator, skip it.
    // This allows us to handle CR, LF, LFCR or CRLF-terminated lines.
    char *next_line = eol;
    if (length > 0) {
        ++next_line;
        const char opposite_terminator = (terminator == 0x0d) ? 0x0a: 0x0d;
        if (*next_line == opposite_terminator) {
            ++next_line; --length;
        }
    }
    *data_ptr = next_line;
    *length_ptr = length;
    return data;
}

// Enter the BASIC program text at data - using arbitrary line terminators -
// a line at a time so BASIC will tokenise it for us.
// TODO: PERHAPS CHANGE "type" TO SOMETHING ELSE, BE CONSISTENT
void type_basic_program(char *data, size_t length) {
    //fprintf(stderr, "SFTODOpQ\n");
    execute_input_line("NEW");
    //fprintf(stderr, "SFTODOQQ\n");

    // Ensure that the last line of the data is terminated by a carriage
    // return, taking advantage of the extra byte allocated by load_binary() to
    // know this is safe.
    data[length] = 0x0d;

    // As with beebasm's PUTBASIC, line numbers are optional on the input. We
    // auto-assign line numbers; line numbers in the input are recognised and
    // used to advance the automatic line number, as long as they don't move
    // it backwards. This allows using line numbers on just a few select lines
    // (e.g. DATA statements) if desired.
    int basic_line_number = 1; // TODO: Allow this and increment to be specified on command line?
    int file_line_number = 1;
    for (char *line = 0; (line = get_line(&data, &length)) != 0; ++file_line_number) {
        error_line_number = file_line_number;

        // Check for a user-specified line number; if we find one we set
        // basic_line_number to the user-specified value and adjust line to
        // skip over the line number.
        size_t leading_space_length = strspn(line, " \t");
        char *line_number_start = line + leading_space_length;
        size_t line_number_length = strspn(line_number_start, "0123456789");
        if (line_number_length > 0) {
            const int buffer_size = 10; // TODO: DUPLICATION OF NAMES WITH OUTER SCOPE
            char buffer[buffer_size];
            check(line_number_length < buffer_size, "Line number too big");
            memcpy(buffer, line_number_start, line_number_length);
            buffer[line_number_length] = '\0';
            int user_line_number = atoi(buffer);
            check(user_line_number >= basic_line_number, "Line number too low");
            basic_line_number = user_line_number;
            line = line_number_start + line_number_length;
        }
        // We now have the line number to use in basic_line_number and the line
        // with no line number at 'line'.

        // Strip leading/trailing spaces if required.
        if (config.strip_leading_spaces) {
            line += strspn(line, " \t");
        }
        if (config.strip_trailing_spaces) {
            int length = strlen(line);
            while ((length > 0) && (strchr(" \t", line[length - 1]) != 0)) {
                --length;
            }
            line[length] = '\0';
        }

        // Generate the fake input for BASIC and pass it over.
        const int buffer_size = 256;
        char buffer[buffer_size];
        check(snprintf(buffer, buffer_size, "%d%s", basic_line_number, line) < buffer_size, "Line too long");
        //fprintf(stderr, "SFTODOLINE!%s!\n", buffer);
        execute_input_line(buffer);

        ++basic_line_number;
    }
    error_line_number = -1;
}

void load_basic(const char *filename) {
    // We load the file as binary data so we can take a look at it and decide
    // whether it's tokenised or text BASIC.
    size_t length;
    char *data = load_binary(filename, &length);
    // http://beebwiki.mdfs.net/Program_format says a Wilson/Acorn format
    // tokenised BASIC program (which is all we care about here) will end with
    // <cr><ff>. TODO: The same page also says it's legit to append data to the
    // end of a BASIC program, so I should probably offer some command line
    // option to override this ("--assume-input-tokenised" or whatever) just in
    // case.
    bool tokenised = ((length >= 2) && (data[length - 2] == '\x0d') && (data[length - 1] == '\xff'));
    // TODO: Print "tokenised" at suitably high verbosity level

    if (tokenised) {
        // Copy the data directly into the emulated machine's memory.
        size_t max_length = himem - page - 512; // arbitrary safety margin
        check(length <= max_length, "Input is too large");
        memcpy(&mpu_memory[page], data, length);
        // Now execute "OLD" so BASIC recognises the program.
        execute_input_line("OLD");
        free(data);
    } else {
        type_basic_program(data, length);
        free(data);
    }





#if 0 // SFTODO!
    XXXX;
    FILE *file = fopen_wrapper(filename, "rb");
    check(file != 0, "Can't open input");
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
#endif
}

void save_basic(const char *filename) {
    FILE *file = fopen_wrapper(filename, "wb");
    check(file != 0, "Can't open output");
    uint16_t top = mpu_read_u16(BASIC_TOP);
    size_t length = top - page;
    size_t bytes_written = fwrite(&mpu_memory[page], 1, length, file);
    check(bytes_written == length, "Error writing output");
    fclose(file);
}

void save_ascii_basic(const char *filename) {
    FILE *file = fopen_wrapper(filename, "wb");
    check(file != 0, "Can't open output");
    // TODO: We need to get the output into the file, which requires thinking
    // about how my OSWRCH etc emulation works. Let's just do a LIST so I can
    // see the output on screen for now.
    assert(output_state == os_discard);
    output_state = os_list_discard_command; output_state_file = file;
    execute_input_line("LIST");
    output_state = os_discard; output_state_file = 0;
    check(fclose(file) == 0, "Error closing output");
}

static const char *no(bool no) {
    return no ? "N" : "Y";
}

void pack(void) {
    execute_input_line("*BUTIL");
    //fprintf(stderr, "SFTODOLLL\n");
    check_pending_output("Ready:"); execute_osrdch("P"); // pack
    check_pending_output("REMs?");
    execute_osrdch(no(config.pack_rems_n));
    check_pending_output("Spaces?");
    execute_osrdch(no(config.pack_spaces_n));
    check_pending_output("Comments?");
    execute_osrdch(no(config.pack_comments_n));
    check_pending_output("Variables?");
    execute_osrdch(no(config.pack_variables_n));
    if (!config.pack_variables_n) {
        check_pending_output("Use unused singles?");
        execute_osrdch(no(config.pack_singles_n));
    }
    check_pending_output("Concatenate?");
    assert(output_state == os_discard);
    output_state = os_pack_discard_concatenate;
    execute_osrdch(no(config.pack_concatenate_n));
    check_pending_output("Ready:"); execute_osrdch("Q"); // quit
    output_state = os_discard;
    execute_input_line("OLD"); // TODO: Because ABE's *FX138 calls are treated as no-op
    //fprintf(stderr, "SFTODOHHH\n");
}

void renumber(void) {
    check_pending_output(">");
    char buffer[256];
    sprintf(buffer, "RENUMBER %d,%d", config.renumber_start, config.renumber_step);
    execute_input_line(buffer);
}

void enter_basic(void) {
    mpu_registers.a = 1; // language entry special value in A
    mpu_registers.x = 0;
    mpu_registers.y = 0;

    const uint16_t code_address = 0x900;
    uint8_t *p = &mpu_memory[code_address];
    *p++ = 0xa2; *p++ = 12;                // LDX #12 TODO: MAGIC CONSTANT
    *p++ = 0x86; *p++ = 0xf4;              // STX &F4
    *p++ = 0x8e; *p++ = 0x30; *p++ = 0xfe; // STX &FE30
    *p++ = 0x4c; *p++ = 0x00; *p++ = 0x80; // JMP &8000 (language entry)

    mpu_registers.s  = 0xff;
    mpu_registers.pc = code_address;
    if (setjmp(mpu_env) == 0) {
        M6502_run(mpu, callback_poll); // never returns
    }
}

void finished(void) {
    save_basic(filenames[1]);
    exit(EXIT_SUCCESS);
}

// TODO: Don't forget to install and test a BRKV handler - wouldn't surprise me if ABE could throw an error if progam is malformed, and of course BASIC could (if only a "line too long" error)

// TODO: Test with invalid input - we don't want to be hanging if we can avoid it

// TODO: Formatting of error messages is very inconsistent, e.g. use of Error: prefix - this is better now, but well worth reviewing later

// TODO: Should create a test suite, which should include input text files with different line terminators and unterminated last lines

// TODO: Should probably test under something like valgrind

// TODO: verbose thoughts
// - level 0 - no output unless something goes wrong, although if output is to stdout of course the BASIC program is shown
// - level 1 - pack shows bytes saved, no more
// - level 2 - pack shows all output
// - so in general verbose is defined on a per-op basis, and has no effect on the tokenise/detokenise stages
// verbose does not control whether we show all output from emulated machine for debugging, that's some separate --debug-foo option

// TODO: I'm being very casual about mixing char/uint8_t/int. It *may* be wise to use char for input/output - that is relatively pure ASCII, although some care is needed as there might be non-ASCII chars mixed in. For cases where I'm dealing with tokenised BASIC or raw 6502 memory not containing (near) ASCII, unsigned char might be a better bet. But think about it.

// TODO: Add option to RENUMBER the program before "saving" it?

// vi: colorcolumn=80
