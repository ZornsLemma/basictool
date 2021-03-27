#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cargs.h"

#define VERSION "0.01"

extern const char *osrdch_queue; // TODO!
void die_help(const char *message); // TODO!
void load_basic(const char *filename); // TODO!
void init(void); // TODO!
void make_service_call(void); // TODO!
void enter_basic(void); // TODO!

const char *filenames[2];

const char *program_name = 0;

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
};

struct {
    int verbose;
    bool filter;
} config = {0, false};

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
        return "SFTODODEFAULTEXENAME";
    }
    return program_name;
}

int main(int argc, char *argv[]) {
    program_name = parse_program_name(argv[0]);

    cag_option_context context;
    cag_option_prepare(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
    while (cag_option_fetch(&context)) {
        char identifier = cag_option_get(&context);
        switch (identifier) {
            case 'h':
                printf("%s " VERSION "\n", program_name);
                printf("Usage: %s [OPTION]... [INPUTFILE] [OUTPUTFILE]\n", program_name);
                printf("SFTODO DESCRIPTION.\n\n");
                cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
                printf(
                        // TODO FORMATTING OF CODE
"\n"
"This program is really a specialised BBC Micro emulator which uses the BBC\n"
"BASIC and Advanced BASIC Editor ROMs to operate on BBC BASIC programs. Use\n"
"--roms to see more information about these ROMs.\n"
);
                // TODO: Show ROM versions? Or on a separate option? Partly
                // depends whether ROM code is compiled in, if it's live-out
                // don't want --help not working because ROMs can't be found.
                return EXIT_SUCCESS;

            case 'r':
                printf(
                        // TODO FORMATTING OF CODE
"This program uses the BBC BASIC and Advanced BASIC Editor ROMs to operate on\n"
"BBC BASIC programs. The ROM headers are as follows:\n"
);
// TODO SHOW ROM HEADERS
                printf(
"\nThe BASIC editor and utilities were originally published separately by\n"
"Altra. The Advanced BASIC Editor ROMs used here are (C) Baildon Electronics.\n"
);
                        // TODO FORMATTING OF CODE

            case 'v':
                ++config.verbose;
                break;

            case 'f':
                config.filter = true;
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
        const char *filename = argv[i];
        if (strcmp(filename, "-") == 0) {
            filename = 0;
        }
        filenames[filename_count] = filename;
    }
    if (i != argc) {
        die_help("Error: Please use a maximum of one input filename and one output filename.");
    }
    // Don't just sit waiting for input on stdin and writing to stdout if we're
    // invoked with no filenames, unless the user explicitly says this is what
    // they want by specifying --filter.
    if ((filename_count == 0) && !config.filter) {
        die_help("Error: Please specify at least one filename or use --filter.");
    }
    // TODO: If the output is binary we should probably also check an option (--filter again?) and refuse to proceed if so. But *maybe* output being stdout will be used to choose a text output option.

    init();
    load_basic(filenames[0]);
    // TODO: The different options should be exposed via command line switches
    osrdch_queue = 
        "P" // Pack
        "Y" // REMs?
        "Y" // Spaces?
        "Y" // Comments?
        "Y" // Variables?
        "Y" // Use unused singles?
        "Y" // Concatenate?
        ;
    enter_basic(); // TODO! make_service_call();
}
// TODO: I should check return value of fclose() everywhere

// vi: colorcolumn=80
