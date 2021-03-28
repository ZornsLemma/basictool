#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cargs.h"
#include "config.h"
#include "data.h"

#define VERSION "0.01"

extern const char *osrdch_queue; // TODO!
void die_help(const char *message); // TODO!
void load_basic(const char *filename); // TODO!
void init(void); // TODO!
void make_service_call(void); // TODO!
void enter_basic(void); // TODO!
void pack(void); // TODO!
void check(bool b, const char *s); // TODO!
void save_basic(const char *filename); // TODO!
void save_ascii_basic(const char *filename); // TODO!

const char *filenames[2] = {0, 0};

const char *program_name = 0;

// TODO: The .identifier values here could probably be char-sized enum values,
// which might be more readable.
static struct cag_option options[] = {
    { .identifier = 'h',
      .access_letters = "h",
      .access_name = "help",
      .description = "show this help and exit" },

    { .identifier = 'r',
      .access_letters = 0,
      .access_name = "roms",
      .description = "show information about the ROMs used and exit" },

    { .identifier = 'v',
      .access_letters = "v",
      .access_name = "verbose",
      .description = "increase verbosity (can be repeated)" },

    { .identifier = 'f',
      .access_letters = 0,
      .access_name = "filter",
      .description = "allow use as a filter (reading from stdin and writing to stdout)" },

    // TODO: These keep... option names are a bit too long and break --help formatting
    { .identifier = 'l',
      .access_letters = 0,
      .access_name = "keep-leading-spaces",
      .description = "don't strip leading spaces from non-tokenised BASIC" },

    { .identifier = 't', // TODO: confusing this isn't "tokenise", which uses 't' for *user* access - a good reason to change to enums maybe
      .access_letters = 0,
      .access_name = "keep-trailing-spaces",
      .description = "don't strip trailing spaces from non-tokenised BASIC" },

    { .identifier = 'p',
      .access_letters = "p",
      .access_name = "pack",
      .description = "pack the program to reduce its size" },

    // TODO: Options to override default Y for pack options

    { .identifier = 'T',
      .access_letters = "t",
      .access_name = "tokenise",
      .description = "output tokenised BASIC" },

    { .identifier = 'a',
      .access_letters = "a",
      .access_name = "ascii",
      .description = "output ASCII text (non-tokenised) BASIC (default)" },

    // TODO: An option to set LISTO for text output (should probably imply text
    // output option)
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

static void check_only_one_token_option(bool new) {
    static int call_count = 0;
    ++call_count;
    if ((call_count == 1) || (config.tokenise_output == new)) {
        return;
    }
    check(call_count == 1, "Please only specify one of --tokenise and --ascii");
}

int main(int argc, char *argv[]) {
    program_name = parse_program_name(argv[0]);

    bool explicit_tokenise_output = false;
    cag_option_context context;
    cag_option_prepare(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
    while (cag_option_fetch(&context)) {
        char identifier = cag_option_get(&context);
        switch (identifier) {
            case 'h':
                printf("%s " VERSION "\n", program_name);
                printf("Usage: %s [OPTION]... [INPUTFILE] [OUTPUTFILE]\n\n", program_name);
                // TODO: "analyse" is only true if I expose 
                        // TODO FORMATTING OF CODE
                printf(
"Tokenise, de-tokenise, examine, pack and generally munge BBC BASIC programs.\n"
"\n"
"This program is really a specialised BBC Micro emulator which uses the BBC\n"
"BASIC and Advanced BASIC Editor ROMs to operate on BBC BASIC programs. Use\n"
"--roms to see more information about these ROMs.\n\n");
                cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
                // TODO: Show ROM versions? Or on a separate option? Partly
                // depends whether ROM code is compiled in, if it's live-out
                // don't want --help not working because ROMs can't be found.
                return EXIT_SUCCESS;

            case 'r':
                show_roms();
                return EXIT_SUCCESS;

            case 'v':
                ++config.verbose;
                break;

            case 'f':
                config.filter = true;
                break;

            case 'l':
                config.strip_leading_spaces = false;
                break;

            case 't':
                config.strip_trailing_spaces = false;
                break;
            
            case 'p':
                config.pack = true;
                break;

            case 'T':
                check_only_one_token_option(true);
                // TODO: Should we require some kind of force option if this
                // is set and we're writing to stdout? But stdout could be a
                // file, should we get unportable and check if stdout is a
                // terminal as well?
                config.tokenise_output = true;
                explicit_tokenise_output = true;
                break;

            case 'a':
                check_only_one_token_option(false);
                config.tokenise_output = false;
                explicit_tokenise_output = true;
                break;

            default:
                fprintf(stderr, "Unrecognised command line identifier: '%c'\n",
                        identifier);
                return EXIT_FAILURE;
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
        // TODO: At the moment, if there's an option after non-option arguments
        // the non-option arguments can be permuted inconsistently. I can't
        // accept this in the long run, but I've raised an issue with cargs
        // upstream so let's see what happens before taking any steps to fix
        // this myself or adjust my command line structure to work round it.
        filenames[filename_count] = filename;
        fprintf(stderr, "SFTODOFILE %i %s\n", filename_count, filename);
    }
    if (i != argc) {
        die_help("Error: Please use a maximum of one input filename and one output filename.");
    }
    // Don't just sit waiting for input on stdin and writing to stdout if we're
    // invoked with no filenames, unless the user explicitly says this is what
    // they want by specifying --filter. TODO: If the user specifies filename
    // "-" this check is bypassed, so maybe we don't need --filter? The message
    // given here could say to use filename "-" to allow this.
    if ((filename_count == 0) && !config.filter) {
        die_help("Error: Please specify at least one filename or use --filter.");
    }
    // TODO: If the output is binary we should probably also check an option (--filter again?) and refuse to proceed if so. But *maybe* output being stdout will be used to choose a text output option.

    if (!explicit_tokenise_output) {
        // TODO: Should we do anything to "intelligently" change the default
        // depending on other options? My current feeling is it's better not
        // to, as it just gets confusing when the program changes its behaviour
        // seemingly at random. If it's not used, get rid of
        // explicit_tokenise_output.
    }

    init(); // TODO: RENAME AS IT'S MAINLY/ALL M6502 INIT
    enter_basic(); // TODO: RENAME START_BASIC()? THO enter_basic2() DOES FEEL BETTER AS 'ENTER'...
    load_basic(filenames[0]); // TODO: rename load_basic_program()? tho symmetry with save would suggest no "_program"
    if (config.pack) {
        pack();
    }
    if (config.tokenise_output) {
        save_basic(filenames[1]); // TODO: rename save_tokenised_basic()
    } else {
        save_ascii_basic(filenames[1]);
    }




#if 0 // SFTODO!
    load_basic(filenames[0]);
    // TODO: The different options should be exposed via command line switches
    osrdch_queue = 
        "P" // Pack
        "Y" // REMs?
        "Y" // Spaces?
        "Y" // Comments?        "Y" // Variables?
        "Y" // Use unused singles?
        "Y" // Concatenate?
        ;
    enter_basic(); // TODO! make_service_call();
#endif
}
// TODO: I should check return value of fclose() everywhere

// vi: colorcolumn=80
