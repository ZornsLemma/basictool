#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cargs.h"
#include "config.h"
#include "emulation.h"
#include "main.h"
#include "utils.h"

#define BASIC_TOP (0x12)

// C-style string maintained by driver_oswrch() to reflect the current line of
// output from the emulated machine.
static char *pending_output = 0;

// Simple state machine used to decide how to handle each line of output from
// the emulated machine.
enum {
    os_discard,
    os_list_discard_command,
    os_format_discard_command,
    os_line_ref_discard_command,
    os_variable_xref_discard_command,
    os_output_non_blank,
    os_pack_discard_concatenate,
    os_pack_discard_blank,
    os_pack
} output_state = os_discard;

// FILE pointer used for "valuable" output we've picked out from the emulated
// machine's output using the state machine.
static FILE *output_file = 0;

static void complete_output_line_handler();

// Replace any non-ASCII characters in 's' with '.'; this is mainly useful in
// avoiding mode 7 colour codes appearing as random characters.
static char *make_printable(char *s) {
    for (char *p = s; *p != '\0'; ++p) {
        // We could use isprint() here, but I don't really want to make any
        // assumptions about the current locale - as 's' originated within
        // the emulated machine, we are really dealling with Acorn ASCII here
        // regardless.
        int c = (unsigned char) *p; // avoid sign extension if char is signed
        if ((c < ' ') || (c > '~')) {
            *p = '.';
        }
    }
    return s;
}

// The low-level emulation code calls this function every time the emulated
// machine calls OSWRCH. It performs very basic terminal emulation to maintain
// a copy of the current line of output as a C string in pending_output.
// Whenever a line feed is written, complete_output_line_handler() is called
// and pending_output is set to an empty string ready for the next line.
// TODO: Review this code fresh to make sure it doesn't have memory leaks or write past bounds etc
// TODO: Call the argument 'c'?
void driver_oswrch(uint8_t data) {
    static size_t pending_output_length = 0;
    static size_t pending_output_cursor_x = 0;
    static size_t pending_output_buffer_size = 0;

    // We just discard NULs in the output; they aren't important for anything
    // we are emulating here.
    if (data == '\0') {
        return;
    }

    // ABE's "pack" repeatedly emits "TOP=&xxxx<cr>" as it crunches the
    // program, so we want to distill that down to the final output by only
    // preserving the final version of the line. We could potentially just
    // reset the stored line to empty in this case, but then we'd be relying on
    // code outputting LFCR at the end of each line rather than CRLF. This
    // would probably work, but it feels a bit brittle, so instead we model CR
    // moving the cursor non-destructively back to the start of the current
    // line.
    if (data == 13) {
        pending_output_cursor_x = 0;
        return;
    }
    if (data == 10) {
        if (config.show_all_output) {
            // make_printable() changes its argument; it probably wouldn't hurt
            // to do this here, but since this is for debugging we don't want
            // to perturb things, so work with a copy.
            char *s = make_printable(ourstrdup(pending_output));
            fprintf(stderr, "bbc:%s\n", s);
            free(s);
        }
        complete_output_line_handler();
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

static bool is_in_pending_output(const char *s) {
    return strstr(pending_output, s) != 0;
}

static void check_is_in_pending_output(const char *s) {
    if (is_in_pending_output(s)) {
        return;
    }
    die("Internal error: Expected to see output containing '%s', got '%s'\n"
        "Try using --show-all-output to see what's going on.", s,
        make_printable(pending_output));
}

// TODO COMMENT
static void print_aligned(const char *s) {
    static const int logical_column_x[] = {0, 50};
    bool spaces_pending = false;
    int logical_column = 0;
    for (int x = 0; *s != '\0'; ++s) {
        char c = *s;
        if (c == ' ') {
            spaces_pending = true;
        } else if (!spaces_pending) {
            putc(c, stderr); ++x;
        } else {
            assert(spaces_pending);
            int align_to_x =
                (logical_column < CAG_ARRAY_SIZE(logical_column_x)) ?
                logical_column_x[logical_column] : 0;
            if ((x >= align_to_x) && (align_to_x != 0)) {
                putc(' ', stderr); ++x;
            } else {
                while (x < align_to_x) {
                    putc(' ', stderr); ++x;
                }
            }
            spaces_pending = false;
            ++logical_column;
            putc(c, stderr); ++x;
        }
    }
    putc('\n', stderr);
}

// This is called by driver_oswrch() when a complete line of output has been
// printed by the emulated machine. It implements a very basic state machine to
// discard noise and write valuable output to output_file.
static void complete_output_line_handler() {
    switch (output_state) {
        case os_discard:
            break;

        case os_list_discard_command:
            check_is_in_pending_output(">LIST");
            // LIST output doesn't contain any blank lines (there's always at
            // least a line number) so this won't lose anything.
            output_state = os_output_non_blank;
            break;

        case os_format_discard_command:
            check_is_in_pending_output("Format listing");
            // Format output doesn't contain any blank lines (there's always at
            // least a line number) so this won't lose anything.
            output_state = os_output_non_blank;
            break;

        case os_line_ref_discard_command:
            check_is_in_pending_output("Table line references");
            output_state = os_output_non_blank;
            break;

        case os_variable_xref_discard_command:
            check_is_in_pending_output("Variables Xref");
            output_state = os_output_non_blank;
            break;

        case os_output_non_blank:
            assert(output_file != 0);
            if (*pending_output != '\0') {
                check(fprintf(output_file, "%s\n", pending_output) >= 0,
                      "Error: Error writing to output file \"%s\"",
                      filenames[1]);
            }
            break;

        case os_pack_discard_concatenate:
            check_is_in_pending_output("Concatenate?");
            output_state = os_pack_discard_blank;
            break;

        case os_pack_discard_blank:
            check(*pending_output == '\0',
                  "Internal error: Expected to see a blank line of output, got \"%s\"",
                  make_printable(pending_output));
            output_state = os_pack;
            break;

        case os_pack: {
            bool is_bytes_saved = is_in_pending_output("Bytes saved");
            if (config.verbose >= 1) {
                if (is_bytes_saved) {
                    fprintf(stderr, "%s\n", make_printable(pending_output));
                } else if (config.verbose >= 2) {
                    make_printable(pending_output);
                    print_aligned(pending_output);
                }
            }
            if (is_bytes_saved) {
                output_state = os_discard;
            }
            break;
        }

        default:
            assert(false);
            break;
    }
}

// Given untokenised ASCII BASIC program text loaded in binary mode using
// load_binary() at 'data' of length 'length', use get_line() to iterate
// through it line-by-line and type it into the emulated machine so BASIC will
// tokenise it for us.
// TODO: PERHAPS CHANGE "type" TO SOMETHING ELSE, BE CONSISTENT
static void type_basic_program(char *data, size_t length) {
    execute_input_line("NEW");

    // Ensure that the last line of the data is terminated by a carriage
    // return, taking advantage of the extra byte allocated by load_binary() to
    // know this is safe.
    data[length] = 0x0d;

    // As with beebasm's PUTBASIC, line numbers are optional on the input. We
    // auto-assign line numbers; line numbers in the input are recognised and
    // used to advance the automatic line number, as long as they don't move
    // it backwards. This allows using line numbers on just a few select lines
    // (e.g. DATA statements) if desired. We don't allow control over the
    // start value and increment here because we provide facilities to renumber
    // any program before we output it, which has the same effect.
    int basic_line_number = 1;
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
        check(snprintf(buffer, buffer_size, "%d%s", basic_line_number, line) < buffer_size, "Error: Line too long");
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
    bool tokenised;
    if (config.input_tokenised) {
        tokenised = true;
    } else {
        // http://beebwiki.mdfs.net/Program_format says a Wilson/Acorn format
        // tokenised BASIC program will end with <cr><ff>.
        tokenised = ((length >= 2) && (data[length - 2] == '\x0d') && (data[length - 1] == '\xff'));
    }
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
}

static void execute_butil(void) {
    execute_input_line("*BUTIL");
    check_is_in_pending_output("Ready:");
    assert(output_state == os_discard);
}

static const char *no(bool no) {
    return no ? "N" : "Y";
}

void pack(void) {
    execute_butil();
    execute_osrdch("P"); // pack
    check_is_in_pending_output("REMs?");
    execute_osrdch(no(config.pack_rems_n));
    check_is_in_pending_output("Spaces?");
    execute_osrdch(no(config.pack_spaces_n));
    check_is_in_pending_output("Comments?");
    execute_osrdch(no(config.pack_comments_n));
    check_is_in_pending_output("Variables?");
    execute_osrdch(no(config.pack_variables_n));
    if (!config.pack_variables_n) {
        check_is_in_pending_output("Use unused singles?");
        execute_osrdch(no(config.pack_singles_n));
    }
    check_is_in_pending_output("Concatenate?");
    assert(output_state == os_discard);
    output_state = os_pack_discard_concatenate;
    execute_osrdch(no(config.pack_concatenate_n));
    check_is_in_pending_output("Ready:"); execute_osrdch("Q"); // quit
    output_state = os_discard;
    // Because *FX138 is implemented as a no-op, ABE's attempt to execute "OLD"
    // won't happen, so do it ourselves.
    execute_input_line("OLD");
}

void renumber(void) {
    check_is_in_pending_output(">");
    char buffer[256];
    sprintf(buffer, "RENUMBER %d,%d", config.renumber_start,
            config.renumber_step);
    execute_input_line(buffer);
}


// Convenience function to close an output file, reporting any errors via die()
// and setting output_file back to null if that's the file being closed.
static void fclose_output(FILE *file, const char *filename) {
    check(fclose(file) == 0, "Error: Error closing output file \"%s\"", filename);
    if (file == output_file) {
        output_file = 0;
    }
}

void save_tokenised_basic(const char *filename) {
    FILE *file = fopen_wrapper(filename, "wb");
    uint16_t top = mpu_read_u16(BASIC_TOP);
    size_t length = top - page;
    size_t bytes_written = fwrite(&mpu_memory[page], 1, length, file);
    check(bytes_written == length, "Error: Error writing to output file \"%s\"", filename);
    fclose_output(file, filename);
}

void save_ascii_basic(const char *filename) {
    output_file = fopen_wrapper(filename, "w");
    assert(output_state == os_discard);
    char buffer[256];
    sprintf(buffer, "LISTO %d", config.listo);
    execute_input_line(buffer);
    output_state = os_list_discard_command;
    execute_input_line("LIST");
    output_state = os_discard;
    fclose_output(output_file, filename);
}

void save_formatted_basic(const char *filename) {
    output_file = fopen_wrapper(filename, "w");
    execute_butil();
    output_state = os_format_discard_command;
    execute_osrdch("F"); // format
    output_state = os_discard;
    fclose_output(output_file, filename);
}

void save_line_ref(const char *filename) {
    output_file = fopen_wrapper(filename, "w");
    execute_butil();
    output_state = os_line_ref_discard_command;
    execute_osrdch("T"); // table line references
    output_state = os_discard;
    fclose_output(output_file, filename);
}

void save_variable_xref(const char *filename) {
    output_file = fopen_wrapper(filename, "w");
    execute_butil();
    output_state = os_variable_xref_discard_command;
    execute_osrdch("V"); // variable xref
    output_state = os_discard;
    fclose_output(output_file, filename);
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

// vi: colorcolumn=80
