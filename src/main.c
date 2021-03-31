#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cargs.h"
#include "config.h"
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

extern const char *osrdch_queue; // TODO!
void load_basic(const char *filename); // TODO!
void init(void); // TODO!
void make_service_call(void); // TODO!
void enter_basic(void); // TODO!
void pack(void); // TODO!
void renumber(void); // TODO!
void check(bool b, const char *s); // TODO!
void save_basic(const char *filename); // TODO!
void save_ascii_basic(const char *filename); // TODO!
void save_formatted_basic(const char *filename); // TODO!
void save_line_ref(const char *filename); // TODO!
void save_variable_xref(const char *filename); // TODO!

const char *filenames[2] = {0, 0};

const char *program_name = 0;

enum option_id {
    oi_help,
    oi_roms,
    oi_verbose,
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
    oi_format,
    oi_line_ref,
    oi_variable_xref,
    oi_tokenise,
    oi_ascii
};

static struct cag_option options[] = {
    { .identifier = oi_help,
      .access_letters = "h",
      .access_name = "help",
      .description = "show this help and exit" },

    { .identifier = oi_roms,
      .access_letters = 0,
      .access_name = "roms",
      .description = "show information about the ROMs used and exit" },

    { .identifier = oi_verbose,
      .access_letters = "v",
      .access_name = "verbose",
      .description = "increase verbosity (can be repeated)" },

    { .identifier = oi_input_tokenised,
      .access_letters = 0,
      .access_name = "input-tokenised",
      .description = "assume the input is tokenised BASIC instead of auto-detecting" },

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
      .description = "pack the program to reduce its size (implied by --pack-*)" },

    // TODO: Arguably these options should be named and described in "standalone"
    // terms, not as if the user is familiar with ABE's pack questions.
 
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

    // TODO: It might be nice to expose ABE's "unpack" option, but my
    // experiments with it (on b-em, not this hacky emulator) suggest it's
    // quite fiddly and may ask you to renumber lines several times. I will
    // leave this for now.

    { .identifier = oi_renumber,
      .access_letters = "r",
      .access_name = "renumber",
      .description = "renumber the program (implied by --renumber-start/step)" },

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

    // TODO: NEED TO GENERATE ERROR IF USER GIVES INCOMPATIBLE OPTIONS, E.G. LISTO WITH TOKENISE, OR FORMAT WITH TOKENISE
    // TODO: REORDER ENUM LIST AND MAIN SWITCH() TO MATCH ORDER
    { .identifier = oi_ascii,
      .access_letters = "a",
      .access_name = "ascii",
      .description = "output ASCII text (non-tokenised) BASIC (default)" },

    { .identifier = oi_tokenise,
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

    // TODO: An option to set LISTO for text output (should probably imply text
    // output option)
};

// TODO: This should probably be printf-like, but check callers - they may not need it
static void die_help(const char *message) {
    die("%s\nTry \"%s --help\" for more information.", message, program_name);
}


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

static int print_to_nul_and_count(const uint8_t *data, int offset, int *width) {
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
        rom_editor_a
    };

    printf(
            // TODO FORMATTING OF CODE
"This program uses the BBC BASIC and Advanced BASIC Editor ROMs to operate on\n"
"BBC BASIC programs. The ROM headers contain the following details:\n"
);

    for (int i = 0; i < CAG_ARRAY_SIZE(roms); ++i) {
        printf("    ");
        int title_and_version_width = 40;
        int version_offset = print_to_nul_and_count(roms[i], 9, &title_and_version_width); // title
        int copyright_offset = roms[i][7];
        if (version_offset < copyright_offset) {
            print_to_nul_and_count(roms[i], version_offset + 1, &title_and_version_width);
        }
        if (title_and_version_width > 0) {
            printf("%*s", title_and_version_width, "");
        }
        printf("(%02x) ", roms[i][8]); // binary version number
        int dummy = 0;
        print_to_nul_and_count(roms[i], copyright_offset + 1, &dummy);
        putchar('\n');
    }

// TODO SHOW ROM HEADERS
    printf(
"\nThe BASIC editor and utilities were originally published separately by\n"
"Altra. The Advanced BASIC Editor ROMs used here are (C) Baildon Electronics.\n"
);
            // TODO FORMATTING OF CODE
}

static long parse_long_argument(const char *name, const char *value, int min, int max) {
    check((value != 0) && (*value != '\0'), "Error: missing value"); // SFTODO: USE 'name' WITH A PRINTF-LIKE CHECK FUNCTION, OR RERWITE AS AN EXPLICIT IF AND USE DIE (OR MAYBE A PRINTF LIKE DIE_HELP);
    char *endptr;
    long result = strtol(value, &endptr, 10);
    check(*endptr == '\0' && (result >= min) && (result <= max), "Error: invalid integer"); // SFTODO: USE 'name' AND 'value' IN THIS MESSAGE AND MIN AND MAX
    return result;
}

int main(int argc, char *argv[]) {
    program_name = parse_program_name(argv[0]);

    cag_option_context context;
    cag_option_prepare(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
    while (cag_option_fetch(&context)) {
        char identifier = cag_option_get(&context);
        switch (identifier) {
            default:
                // This occurs for unsupported options entered by the user,
                // not just options we've told cargs about but forgotten to
                // implement, so fall through to --help.
            case oi_help:
                printf("%s " VERSION "\n", program_name);
                printf("Usage: %s [OPTION]... INPUTFILE [OUTPUTFILE]\n", program_name);
                printf("INPUTFILE should be ASCII or tokenised BBC BASIC.\n");
                printf("(A filename of \"-\" indicates standard input/output.)\n\n");
                        // TODO FORMATTING OF CODE
                printf(
"Tokenise, de-tokenise, pack and analyse BBC BASIC programs.\n"
"\n"
"This program is really a specialised BBC Micro emulator which uses the BBC\n"
"BASIC and Advanced BASIC Editor ROMs to operate on BBC BASIC programs. Use\n"
"--roms to see more information about these ROMs.\n\n");
                cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
                return EXIT_SUCCESS;

            case oi_roms:
                show_roms();
                return EXIT_SUCCESS;

            case oi_verbose:
                ++config.verbose;
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

            case oi_format:
                config.format = true;
                break;

            case oi_line_ref:
                config.line_ref = true;
                break;

            case oi_variable_xref:
                config.variable_xref = true;
                break;

            case oi_tokenise:
                // TODO: Should we require some kind of force option if this
                // is set and we're writing to stdout? But stdout could be a
                // file, should we get unportable and check if stdout is a
                // terminal as well?
                config.tokenise_output = true;
                break;

            case oi_ascii:
                config.ascii_output = true;
                break;
        }
    }

    //printf("config.verbose %d\n", config.verbose); // TODO: RESPECT THIS!
    int filename_count = 0;
    const int max_filenames = CAG_ARRAY_SIZE(filenames);
    int i;
    for (i = context.index; (i < argc) && (filename_count < max_filenames);
         ++i, ++filename_count) {
        const char *filename = context.argv[i];
        if (strcmp(filename, "-") == 0) {
            filename = 0;
        }
        filenames[filename_count] = filename;
        //fprintf(stderr, "SFTODOFILE %i %s\n", filename_count, filename);
    }
    if (i != argc) {
        die_help("Error: Please use a maximum of one input filename and one output filename.");
    }
    // Don't just sit waiting for input on stdin and writing to stdout if we're
    // invoked with no filenames. This is a supported mode of operation, but to
    // avoid confusion we require at least one "-" argument to be specified.
    if (filename_count == 0) {
        die_help("Error: Please give at least one filename; use input filename \"-\" for standard input.");
    }
    // TODO: If the output is binary we should probably also check an option (--filter again?) and refuse to proceed if so. But *maybe* output being stdout will be used to choose a text output option.

    int output_options = 0;
    COUNT_BOOL(output_options, config.format);
    COUNT_BOOL(output_options, config.line_ref);
    COUNT_BOOL(output_options, config.variable_xref);
    COUNT_BOOL(output_options, config.tokenise_output);
    COUNT_BOOL(output_options, config.ascii_output);
    if (output_options == 0) {
        config.ascii_output = true;
    } else if (output_options > 1) {
        die_help("Error: Please don't use more than one output type option.");
    }

    init(); // TODO: RENAME AS IT'S MAINLY/ALL M6502 INIT
    enter_basic(); // TODO: RENAME START_BASIC()? THO enter_basic2() DOES FEEL BETTER AS 'ENTER'...
    load_basic(filenames[0]); // TODO: rename load_basic_program()? tho symmetry with save would suggest no "_program"
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
    } else if (config.tokenise_output) {
        save_basic(filenames[1]); // TODO: rename save_tokenised_basic()
    } else {
        assert(config.ascii_output);
        save_ascii_basic(filenames[1]);
    }
}
// TODO: I should check return value of fclose() everywhere

// TODO: It might be nice to offer option to strip line numbers on non-tokenised
// output. However, that would break programs which need to use line numbers,
// so it might be nice to do that in conjunction with the ABE "table line
// references" option to try (we'd never get it perfect, due to things like
// calculated line numbers) to remove line numbers except where they're not
// used.

// TODO: Should I make sure all printf() etc output and --help fits in 80 cols or is wrapped nicely?

// vi: colorcolumn=80
