// A general note on the use of stdout and stderr in this code:
//
// - --help and --roms write to stdout, since producing that output is their
//   main function. (No one is likely to care either way about this.)
//
// - If no output filename is provided, the final version of the BASIC program
//   is written to stdout. This seems reasonable and is useful if this tool
//   is being used in a pipeline.
//
// - Given the previous point, output from --verbose and --show-all-output is
//   written to stderr so it doesn't get mixed in with the BASIC program.
//
// - Errors are written to stderr, again so they don't get redirected when the
//   user might only have intended to redirect the output BASIC program.

// A collection of general TODOs:
//
// TODO: Support for HIBASIC might be nice (only for tokenising/detokenising;
// ABE runs at &8000 so probably can't work with HIBASIC-sized programs), but
// let's not worry about that yet.
//
// TODO: It might be nice to expose ABE's "unpack" option, but my experiments
// with it (on b-em, not this hacky emulator) suggest it's quite fiddly and may
// ask you to renumber lines several times. I will leave this for now.
//
// TODO: It might be nice to offer an option to strip line numbers on
// non-tokenised output. However, that would break programs which need to use
// line numbers, so it might be nice to do that in conjunction with the ABE
// "table line references" option to try (we'd never get it perfect, due to
// things like calculated line numbers) to remove line numbers except where
// they're not used.

#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cargs.h"
#include "config.h"
#include "driver.h"
#include "emulation.h"
#include "roms.h"
#include "utils.h"

#define VERSION "0.01"

// We could just sum the bool variables, but let's play it safe in case we
// want to support pre-C99 compilers where bool is a typedef for something
// like int and may have values other than 0 and 1.
#define COUNT_BOOL(count, b) \
    do { \
        if (b) { \
            ++count; \
       } \
    } while (0)

const char *program_name = 0;
const char *filenames[2] = {"-", "-"};

enum option_id {
    oi_help,
    oi_roms,
    oi_verbose,
    oi_show_all_output,
    oi_input_tokenised,
    oi_keep_spaces,
    oi_keep_spaces_start,
    oi_keep_spaces_end,
    oi_pack,
    oi_pack_rems_n,
    oi_pack_spaces_n,
    oi_pack_comments_n,
    oi_pack_variables_n,
    oi_pack_singles_n,
    oi_pack_concatenate_n,
    oi_renumber,
    oi_renumber_start,
    oi_renumber_step,
    oi_listo,
    oi_output_ascii,
    oi_output_tokenised,
    oi_format,
    oi_line_ref,
    oi_variable_xref
};

// These options are roughly ordered so that they follow the order of
// processing. First we load the program (so we have options related to
// loading), then we maybe pack (so options related to packing are next), then
// we output something (so options related to that are next).
static struct cag_option options[] = {
    { .identifier = oi_help,
      .access_letters = "h",
      .access_name = "help",
      .description = "show this help and exit" },

    { .identifier = oi_roms,
      .access_letters = 0,
      .access_name = "roms",
      .description = "show information about ROMs used and exit" },

    { .identifier = oi_verbose,
      .access_letters = "v",
      .access_name = "verbose",
      .description = "increase verbosity (can be repeated)" },

    { .identifier = oi_show_all_output,
      .access_letters = 0,
      .access_name = "show-all-output",
      .description = "show all output from emulated machine" },

    { .identifier = oi_input_tokenised,
      .access_letters = 0,
      .access_name = "input-tokenised",
      .description = "assume input is tokenised BASIC, don't auto-detect" },

    { .identifier = oi_keep_spaces,
      .access_letters = "k",
      .access_name = "keep-spaces",
      .description = "don't strip spaces at start/end of lines when tokenising" },

    { .identifier = oi_keep_spaces_start,
      .access_letters = 0,
      .access_name = "keep-spaces-start",
      .description = "don't strip spaces at start of lines when tokenising" },

    { .identifier = oi_keep_spaces_end,
      .access_letters = 0,
      .access_name = "keep-spaces-end",
      .description = "don't strip spaces at end of lines when tokenising" },

    { .identifier = oi_pack,
      .access_letters = "p",
      .access_name = "pack",
      .description = "pack program to reduce its size (implied by --pack-*)" },

    // TODO: Arguably these options should be named and described in
    // "standalone" terms, not as if the user is familiar with ABE's pack
    // questions.
 
    { .identifier = oi_pack_rems_n,
      .access_letters = "",
      .access_name = "pack-rems-n",
      .description = "answer N to \"REMs?\" question when packing" },

    { .identifier = oi_pack_spaces_n,
      .access_letters = "",
      .access_name = "pack-spaces-n",
      .description = "answer N to \"Spaces?\" question when packing" },

    { .identifier = oi_pack_comments_n,
      .access_letters = "",
      .access_name = "pack-comments-n",
      .description = "answer N to \"Comments?\" question when packing" },

    { .identifier = oi_pack_variables_n,
      .access_letters = "",
      .access_name = "pack-variables-n",
      .description = "answer N to \"Variables?\" question when packing" },

    { .identifier = oi_pack_singles_n,
      .access_letters = "",
      .access_name = "pack-singles-n",
      .description = "answer N to \"Use unused singles?\" question when packing" },

    { .identifier = oi_pack_concatenate_n,
      .access_letters = "",
      .access_name = "pack-concatenate-n",
      .description = "answer N to \"Concatenate?\" question when packing" },

    { .identifier = oi_renumber,
      .access_letters = "r",
      .access_name = "renumber",
      .description = "renumber program (implied by --renumber-start/step)" },

    { .identifier = oi_renumber_start,
      .access_letters = 0,
      .access_name = "renumber-start",
      .value_name = "N",
      .description = "renumber starting from N" },

    { .identifier = oi_renumber_step,
      .access_letters = 0,
      .access_name = "renumber-step",
      .value_name = "N",
      .description = "renumber so line numbers increment by N" },

    { .identifier = oi_listo,
      .access_letters = "l",
      .access_name = "listo",
      .value_name = "N",
      .description = "use LISTO N to indent ASCII output\n\nOutput type options (pick one only):" },

    // TODO: REORDER ENUM LIST AND MAIN SWITCH() TO MATCH ORDER
    { .identifier = oi_output_ascii,
      .access_letters = "a",
      .access_name = "ascii",
      .description = "output ASCII text (non-tokenised) BASIC (default)" },

    { .identifier = oi_output_tokenised,
      .access_letters = "t",
      .access_name = "tokenise",
      .description = "output tokenised BASIC" },

    { .identifier = oi_format,
      .access_letters = "f",
      .access_name = "format",
      .description = "output formatted ASCII text (non-tokenised) BASIC" },

    { .identifier = oi_line_ref,
      .access_letters = 0,
      .access_name = "line-ref",
      .description = "output table of line references" },

    { .identifier = oi_variable_xref,
      .access_letters = 0,
      .access_name = "variable-xref",
      .description = "output variable cross references" },
};

// argv[0] will contain the program name, but if we're not being run from the
// PATH it may contain a (potentially quite long) path prefix of some kind.
// parse_program_name() makes a reasonably cross-platform stab at getting the
// leafname.

static const char *parse_program_name_internal(const char *name) {
    if (name == 0) {
        return "";
    }
    const char *final_slash     = strrchr(name, '/');
    const char *final_backslash = strrchr(name, '\\');
    if ((final_slash != 0) && (final_backslash != 0)) {
        return (final_slash > final_backslash) ? (final_slash + 1) :
                                                 (final_backslash + 1);
    }
    if (final_slash != 0) {
        return final_slash + 1;
    }
    if (final_backslash != 0) {
        return final_backslash + 1;
    }
    return name;
}

static const char *parse_program_name(const char *name) {
    const char *program_name = parse_program_name_internal(name);
    if (*program_name == '\0') {
        return "basictool";
    }
    return program_name;
}

static int print_to_nul_and_count(const uint8_t *data, int offset, int *width)
{
    int c;
    bool output = false;
    for (; (c = data[offset]) != '\0'; ++offset, --(*width)) {
        if ((c >= ' ') && (c <= '~')) {
            putchar((char) c);
            output = true;
        }
    }
    if (output) {
        putchar(' ');
        --(*width);
    }
    return offset;
}

static void show_roms(void) {
    const uint8_t *roms[] = {
        rom_basic,
        rom_editor_b
    };

    printf(
"This program uses the BBC BASIC and Advanced BASIC Editor ROMs to operate on\n"
"BBC BASIC programs. The ROM headers contain the following details:\n"
        );

    for (int i = 0; i < CAG_ARRAY_SIZE(roms); ++i) {
        printf("    ");
        int title_and_version_width = 40;
        const int title_offset = 9;
        int version_offset = print_to_nul_and_count(
            roms[i], title_offset, &title_and_version_width);
        int copyright_offset = roms[i][7];
        if (version_offset < copyright_offset) {
            print_to_nul_and_count(roms[i], version_offset + 1,
                                   &title_and_version_width);
        }
        if (title_and_version_width > 0) {
            printf("%*s", title_and_version_width, "");
        }
        printf("(%02x) ", roms[i][8]); // binary version number
        int dummy = 0;
        print_to_nul_and_count(roms[i], copyright_offset + 1, &dummy);
        putchar('\n');
    }

    printf(
"\nThe BASIC editor and utilities were originally published separately by\n"
"Altra. The Advanced BASIC Editor ROMs used here are (C) Baildon Electronics.\n"
    );
}

static long parse_long_argument(const char *name, const char *value, int min,
                                int max) {
    if ((value == 0) || (*value == '\0')) {
        die_help("Error: missing value for %s", name);
    }
    char *endptr;
    long result = strtol(value, &endptr, 10);
    if ((*endptr != '\0') || (result < min) || (result > max)) {
        die_help("Error: invalid %s value \"%s\"", name, value);
    }
    return result;
}

int main(int argc, char *argv[]) {
    program_name = parse_program_name(argv[0]);

    cag_option_context context;
    cag_option_prepare(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
    while (cag_option_fetch(&context)) {
        char identifier = cag_option_get(&context);
        switch (identifier) {
            case oi_help:
                printf(
"%s " VERSION "\n"
"Usage: %s [OPTION]... INPUTFILE [OUTPUTFILE]\n"
"INPUTFILE should be ASCII text (non-tokenised) or tokenised BBC BASIC.\n"
"(A filename of \"-\" indicates standard input/output.)\n"
"\n"
"Tokenise, de-tokenise, pack and analyse BBC BASIC programs.\n"
"\n"
"This program is really a specialised BBC Micro emulator which uses the BBC\n"
"BASIC and Advanced BASIC Editor ROMs to operate on BBC BASIC programs. Use\n"
"--roms to see more information about these ROMs.\n\n",
                    program_name, program_name);
                cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
                return EXIT_SUCCESS;

            case oi_roms:
                show_roms();
                return EXIT_SUCCESS;

            case oi_verbose:
                ++config.verbose;
                break;

            case oi_show_all_output:
                config.show_all_output = true;
                break;

            case oi_input_tokenised:
                config.input_tokenised = true;
                break;

            case oi_keep_spaces:
                config.strip_leading_spaces = false;
                config.strip_trailing_spaces = false;
                break;

            case oi_keep_spaces_start:
                config.strip_leading_spaces = false;
                break;

            case oi_keep_spaces_end:
                config.strip_trailing_spaces = false;
                break;
            
            case oi_pack:
                config.pack = true;
                break;

            case oi_pack_rems_n:
                config.pack = true;
                config.pack_rems_n = true;
                break;

            case oi_pack_spaces_n:
                config.pack = true;
                config.pack_spaces_n = true;
                break;

            case oi_pack_comments_n:
                config.pack = true;
                config.pack_comments_n = true;
                break;

            case oi_pack_variables_n:
                config.pack = true;
                config.pack_variables_n = true;
                break;

            case oi_pack_singles_n:
                config.pack = true;
                config.pack_singles_n = true;
                break;

            case oi_pack_concatenate_n:
                config.pack = true;
                config.pack_concatenate_n = true;
                break;

            case oi_renumber:
                config.renumber = true;
                break;

            case oi_renumber_start:
                config.renumber = true;
                config.renumber_start = (int) parse_long_argument(
                    "--renumber-start", cag_option_get_value(&context),
                    0, 32767);
                break;

            case oi_renumber_step:
                config.renumber = true;
                config.renumber_step = (int) parse_long_argument(
                    "--renumber-step", cag_option_get_value(&context),
                    1, 255);
                break;

            case oi_listo:
                config.listo = (int) parse_long_argument(
                    "--listo", cag_option_get_value(&context), 0, 7);
                break;

            case oi_output_ascii:
                config.output_ascii = true;
                break;

            case oi_output_tokenised:
                // TODO: Should we require some kind of force option if this
                // is set and we're writing to stdout? But stdout could be a
                // file, should we get unportable and check if stdout is a
                // terminal as well?
                config.output_tokenised = true;
                break;

            case oi_format:
                config.format = true;
                break;

            case oi_line_ref:
                config.line_ref = true;
                break;

            case oi_variable_xref:
                config.variable_xref = true;
                break;

            default:
                die_help("Error: Unrecognised option \"%s\"",
                         argv[cag_option_get_index(&context) - 1]);
                break;
        }
    }

    int filename_count = 0;
    const int max_filenames = CAG_ARRAY_SIZE(filenames);
    int i;
    for (i = context.index; (i < argc) && (filename_count < max_filenames);
         ++i, ++filename_count) {
        filenames[filename_count] = context.argv[i];
    }
    if (i != argc) {
        die_help("Error: Please use a maximum of one input filename and one "
                 "output filename.");
    }
    // Don't just sit waiting for input on stdin and writing to stdout if we're
    // invoked with no filenames. This is a supported mode of operation, but to
    // avoid confusion we require at least one "-" argument to be specified.
    if (filename_count == 0) {
        die_help("Error: Please give at least one filename; use input "
                 "filename \"-\" for standard input.");
    }

    int output_options = 0;
    COUNT_BOOL(output_options, config.format);
    COUNT_BOOL(output_options, config.line_ref);
    COUNT_BOOL(output_options, config.variable_xref);
    COUNT_BOOL(output_options, config.output_tokenised);
    COUNT_BOOL(output_options, config.output_ascii);
    if (output_options == 0) {
        config.output_ascii = true;
    } else if (output_options > 1) {
        die_help("Error: Please don't use more than one output type option.");
    }

    if (config.listo == -1) {
        config.listo = 0;
    } else {
        if (!config.output_ascii) {
            warn("--listo only has an effect with the --ascii output type");
        }
    }

    emulation_init();
    load_basic(filenames[0]);
    if (config.pack) {
        pack();
    }
    if (config.renumber) {
        renumber();
    }
    if (config.format) {
        save_formatted_basic(filenames[1]);
    } else if (config.line_ref) {
        save_line_ref(filenames[1]);
    } else if (config.variable_xref) {
        save_variable_xref(filenames[1]);
    } else if (config.output_tokenised) {
        save_tokenised_basic(filenames[1]);
    } else {
        assert(config.output_ascii);
        save_ascii_basic(filenames[1]);
    }
}

// TODO: Should I make sure all printf() etc output and --help fits in 80 cols or is wrapped nicely?

// vi: colorcolumn=80
