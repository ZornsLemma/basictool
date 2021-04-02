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
    os_variable_xref_output,
    os_output_non_blank,
    os_pack_discard_concatenate,
    os_pack_discard_blank,
    os_pack_output
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
void driver_oswrch(uint8_t c) {
    // These three static variables track state related to pending_offset;
    // since they aren't in the global namespace, we can use shorter names.
    static size_t po_length = 0;
    static size_t po_cursor_x = 0;
    static size_t po_buffer_size = 0;

    // We just discard NULs in the output; they aren't important for anything
    // we are emulating here and they're not compatible with our strategy of
    // pending_output being a C-style string.
    if (c == '\0') {
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
    if (c == cr) {
        po_cursor_x = 0;
        return;
    }
    if (c == lf) {
        if (config.show_all_output) {
            // make_printable() changes its argument; it probably wouldn't hurt
            // to do this here, but since this is for debugging we don't want
            // to perturb things so work with a copy.
            char *s = make_printable(ourstrdup(pending_output));
            fprintf(stderr, "bbc:%s\n", s);
            free(s);
        }
        complete_output_line_handler();
        po_length = po_cursor_x = 0;
        pending_output[0] = '\0';
        return;
    }

    if ((po_cursor_x + 2) > po_buffer_size) {
        if (po_buffer_size == 0) {
            po_buffer_size = 4; // TODO: make 64 or 128 or something
        } else {
            po_buffer_size *= 2;
        }
        pending_output = check_alloc(realloc(pending_output, po_buffer_size));
    }

    pending_output[po_cursor_x] = c;
    pending_output[max(po_cursor_x, po_length) + 1] ='\0';
    po_cursor_x += 1;
    po_length = max(po_cursor_x, po_length);
    assert(strlen(pending_output) == po_length);
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

static void putc_wrapper(int c, FILE *file) {
    check(putc(c, file) != EOF, "Error: Error writing to output file \"%s\"",
          filenames[1]);
}

// Write 's' to 'file', adjusting the spaces so the columns line up nicely.
static void print_aligned(FILE *file, const char *s) {
    static const int logical_column_x[] = {0, 50};
    bool spaces_pending = false;
    int logical_column = 0;
    int x = 0;
    for (; *s != '\0'; ++s) {
        char c = *s;
        if (c == ' ') {
            spaces_pending = true;
        } else if (!spaces_pending) {
            putc_wrapper(c, file); ++x;
        } else {
            assert(spaces_pending);

            int align_to_x;
            if (logical_column < CAG_ARRAY_SIZE(logical_column_x)) {
                align_to_x = logical_column_x[logical_column];
            } else {
                align_to_x = logical_column_x[1] + 6 * (logical_column - 1);
            }

            if (x >= align_to_x) {
                putc_wrapper(' ', file); ++x;
            } else {
                while (x < align_to_x) {
                    putc_wrapper(' ', file); ++x;
                }
            }
            spaces_pending = false;
            ++logical_column;
            putc_wrapper(c, file); ++x;
        }
    }
    putc_wrapper('\n', file);
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
            output_state = os_variable_xref_output;
            break;

        case os_variable_xref_output:
            assert(output_file != 0);
            if (*pending_output != '\0') {
                make_printable(pending_output);
                print_aligned(output_file, pending_output);
            }
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
            output_state = os_pack_output;
            break;

        case os_pack_output: {
            bool is_bytes_saved = is_in_pending_output("Bytes saved");
            if (config.verbose >= 1) {
                if (is_bytes_saved) {
                    fprintf(stderr, "%s\n", make_printable(pending_output));
                } else if (config.verbose >= 2) {
                    make_printable(pending_output);
                    print_aligned(stderr, pending_output);
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

// Return the line number to use for the line of BASIC at *lineptr; this will
// be the number at the start of *lineptr if there is one, line_number if there
// isn't. *lineptr is advanced to skip over the line number, if any.
static int get_line_number(char **lineptr, int line_number) {
    assert(lineptr != 0);
    char *line = *lineptr;
    assert(line != 0);

    const int max_line_number = 32767;
    char *number_start = line + strspn(line, " \t");
    int number_length = strspn(number_start, "0123456789");
    if (number_length > 0) {
        char *end;
        long l = strtol(number_start, &end, 10);
        check((end == number_start + number_length) && (l <= max_line_number),
              "Line number %.*s is too large", number_length, number_start);
        int user_line_number = (int) l;

        check(user_line_number >= line_number,
              "Line number %d is less than previous line number %d",
              user_line_number, line_number - 1);
        line_number = user_line_number;
        *lineptr = number_start + number_length;
    }

    check(line_number <= max_line_number, "Line number %d is too large",
          line_number);
    return line_number;
}

// Given untokenised ASCII BASIC program text loaded in binary mode using
// load_binary() at 'data' of length 'length', use get_line() to iterate
// through it line-by-line and type it into the emulated machine so BASIC will
// tokenise it for us.
// TODO: PERHAPS CHANGE "type" TO SOMETHING ELSE, BE CONSISTENT
static void type_basic_program(char *data, size_t length) {
    execute_input_line("NEW");

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

        basic_line_number = get_line_number(&line, basic_line_number);
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
        if (config.verbose >= 1) {
            info("Input auto-detected as %s BASIC",
                 tokenised ? "tokenised" : "ASCII text (non-tokenised)");
        }
    }

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

// TODO: Test with invalid input - we don't want to be hanging if we can avoid it

// TODO: Formatting of error messages is very inconsistent, e.g. use of Error: prefix - this is better now, but well worth reviewing later

// TODO: Should create a test suite, which should include input text files with different line terminators and unterminated last lines - I should probably bundle the test files up in a .tar.gz so git can't mangle the line endings, although this is perhaps a bit error prone (I edit a test file and it is corrupted) - perhaps the test suite should "echo" the line-ending sensitive tests to create them on the fly, then I can use git for everything else

// TODO: Should probably test under something like valgrind

// TODO: I'm being very casual about mixing char/uint8_t/int. It *may* be wise to use char for input/output - that is relatively pure ASCII, although some care is needed as there might be non-ASCII chars mixed in. For cases where I'm dealing with tokenised BASIC or raw 6502 memory not containing (near) ASCII, unsigned char might be a better bet. But think about it.

// vi: colorcolumn=80
