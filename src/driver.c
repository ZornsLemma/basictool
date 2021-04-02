#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "emulation.h"
#include "utils.h"

#define BASIC_TOP (0x12)

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
static FILE *output_state_file = 0; // TODO: RENAME? REMOVE "STATE"?
static const char *output_state_filename = 0;

static char *pending_output = 0;
static size_t pending_output_length = 0;
static size_t pending_output_cursor_x = 0;
static size_t pending_output_buffer_size = 0;

// TODO: Support for HIBASIC might be nice (only for tokenising/detokenising;
// ABE runs at &8000 so probably can't work with HIBASIC-sized programs), but
// let's not worry about that yet.

static int max(int lhs, int rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

// A simple implementation of strdup() so we don't assume it's available; it's
// not part of C99.
static char *ourstrdup(const char *s) {
    char *t = malloc(strlen(s) + 1);
    strcpy(t, s);
    return t;
}

// TODO: For stdout to be useful, I need to be sure all verbose output etc is written to stderr - maybe not, it depends how you view the verbose output. A user might want to do "basictool input.txt --pack -vv output.tok > pack-output.txt"; if we output to stderr this redirection becomes fiddlier.
static FILE *fopen_wrapper(const char *pathname, const char *mode) {
    if (pathname == 0) {
        assert((mode != 0) && (*mode != '\0'));
        // We ignore the presence of a "b" in mode; I don't think there's a
        // portable way to re-open stdin/stdout in binary mode. TODO: Should we
        // generate an error if there's a "b"? But this is probably fine on
        // Unix-like systems.
        if (mode[0] == 'r') {
            return stdin;
        } else if (mode[0] == 'w') {
            return stdout;
        } else {
            die("Internal error: Invalid mode \"%s\" passed to fopen_wrapper()", mode);
        }
    } else {
        return fopen(pathname, mode);
    }
}

// TODO: Output should ultimately be gated via a -v option, perhaps with some
// sort of (optional but default) filtering to tidy it up

static bool is_in_pending_output(const char *s) {
    return strstr(pending_output, s) != 0;
}

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

static void check_pending_output(const char *s) {
    if (is_in_pending_output(s)) {
        return;
    }
    // TODO: Perhaps suggest use of the debug output help option in this message? (On a new line after the existing message)
    die("Error: Expected to see output containing '%s', got '%s'", s,
        make_printable(pending_output));
}


static void complete_output_line_handler(char *line) {
    switch (output_state) {
        case os_discard:
            break;

        case os_list_discard_command:
            check_pending_output(">LIST");
            // LIST output doesn't contain any blank lines (there's always at
            // least a line number) so this won't lose anything.
            output_state = os_output_non_blank;
            break;

        case os_format_discard_command:
            check_pending_output("Format listing");
            // Format output doesn't contain any blank lines (there's always at
            // least a line number) so this won't lose anything.
            output_state = os_output_non_blank;
            break;

        case os_line_ref_discard_command:
            check_pending_output("Table line references");
            output_state = os_output_non_blank;
            break;

        case os_variable_xref_discard_command:
            check_pending_output("Variables Xref");
            output_state = os_output_non_blank;
            break;

        case os_output_non_blank:
            assert(output_state_file != 0);
            if (*line != '\0') {
                check(fprintf(output_state_file, "%s\n", line) >= 0,
                      "Error: Error writing to output file \"%s\"",
                      output_state_filename);
            }
            break;

        case os_pack_discard_concatenate:
            check_pending_output("Concatenate?");
            output_state = os_pack_discard_blank;
            break;

        case os_pack_discard_blank:
            check(*pending_output == '\0',
                  "Error: Expected to see a blank line of output, got \"%s\"",
                  make_printable(line));
            output_state = os_pack;
            break;

        case os_pack:
            if (config.verbose >= 1) {
                bool is_bytes_saved = is_in_pending_output("Bytes saved");
                if (is_bytes_saved || (config.verbose >= 2)) {
                    // TODO: We could maybe force alignment of the columns
                    // in the verbose>=2 output
                    // TODO: Should this go to stderr or stdout?
                    fprintf(stderr, "%s\n", make_printable(line));
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

// TODO: Review this code fresh to make sure it doesn't have memory leaks or write past bounds etc
void pending_output_insert(uint8_t data) {
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

// Read a file into a malloc()-ed block of memory. The pointer to the
// malloc()-ed block is returned and *length is set to the length.
static char *load_binary(const char *filename, size_t *length) {
    assert(length != 0);
    FILE *file = fopen_wrapper(filename, "rb");
    check(file != 0, "Error: Can't open input file \"%s\"", filename);
    // Since we're dealing with BASIC programs on a 32K-ish machine, we don't
    // need to handle arbitrarily large files.
    const int max_size = 64 * 1024;
    char *data = check_alloc(malloc(max_size));
    *length = fread(data, 1, max_size, file);
    check(!ferror(file), "Error: Error reading from input file \"%s\"", filename);
    check(feof(file), "Error: Input file \"%s\" is too large", filename);
    check(fclose(file) == 0, "Error: Error closing input file \"%s\"", filename);
    // We allocate an extra byte so we can easily guarantee that the last line
    // ends with a line terminator when reading non-tokenised input.
    return check_alloc(realloc(data, (*length) + 1));
}

// TODO: COMMENT
// TODO: Review this later, I think there are no missing corner cases (bearing in mind we deliberately put a CR at the end of the input to catch unterminated last lines) but a fresh look would be good
static char *get_line(char **data_ptr, size_t *length_ptr) {
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

// TODO: RENAME THIS, AND RENAME FOPEN_WRAPPER()?
static FILE *fopen_output_wrapper(const char *filename, const char *mode) {
    FILE *file = fopen_wrapper(filename, mode);
    check(file != 0, "Error: Can't open output file \"%s\"", filename);
    output_state_file = file;
    // SFTODO: This will go wrong if we get an error writing to stdout
    output_state_filename = filename;
    return file;
}

static void fclose_output(FILE *file, const char *filename) {
    check(fclose(file) == 0, "Error: Error closing output file \"%s\"", filename);
    output_state_file = 0;
    output_state_filename = 0;
}

void save_basic(const char *filename) {
    FILE *file = fopen_output_wrapper(filename, "wb");
    uint16_t top = mpu_read_u16(BASIC_TOP);
    size_t length = top - page;
    size_t bytes_written = fwrite(&mpu_memory[page], 1, length, file);
    check(bytes_written == length, "Error: Error writing to output file \"%s\"", filename);
    fclose_output(file, filename);
}

void save_ascii_basic(const char *filename) {
    FILE *file = fopen_output_wrapper(filename, "w");
    assert(output_state == os_discard);
    char buffer[256];
    sprintf(buffer, "LISTO %d", config.listo);
    execute_input_line(buffer);
    output_state = os_list_discard_command;
    execute_input_line("LIST");
    output_state = os_discard;
    fclose_output(file, filename);
}

static void execute_butil(void) {
    execute_input_line("*BUTIL");
    check_pending_output("Ready:");
    assert(output_state == os_discard);
}

void save_formatted_basic(const char *filename) {
    FILE *file = fopen_output_wrapper(filename, "w");
    execute_butil();
    output_state = os_format_discard_command;
    execute_osrdch("F"); // format
    output_state = os_discard;
    fclose_output(file, filename);
}

void save_line_ref(const char *filename) {
    FILE *file = fopen_output_wrapper(filename, "w");
    execute_butil();
    output_state = os_line_ref_discard_command;
    execute_osrdch("T"); // table line references
    output_state = os_discard;
    fclose_output(file, filename);
}

void save_variable_xref(const char *filename) {
    FILE *file = fopen_output_wrapper(filename, "w");
    execute_butil();
    output_state = os_variable_xref_discard_command;
    execute_osrdch("V"); // variable xref
    output_state = os_discard;
    fclose_output(file, filename);
}

static const char *no(bool no) {
    return no ? "N" : "Y";
}

void pack(void) {
    execute_butil();
    execute_osrdch("P"); // pack
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
    // Because *FX138 is implemented as a no-op, ABE's attempt to execute "OLD"
    // won't happen, so do it ourselves.
    execute_input_line("OLD");
}

void renumber(void) {
    check_pending_output(">");
    char buffer[256];
    sprintf(buffer, "RENUMBER %d,%d", config.renumber_start,
            config.renumber_step);
    execute_input_line(buffer);
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
