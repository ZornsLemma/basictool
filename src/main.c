#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cargs.h"

extern const char *osrdch_queue; // TODO!
void die_help(const char *message); // TODO!
void load_basic(const char *filename); // TODO!
void init(void); // TODO!
void make_service_call(void); // TODO!

const char *filenames[2];

static struct cag_option options[] = {
    { .identifier = 'h',
      .access_letters = "h",
      .access_name = "help",
      .description = "show this help and exit" },

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

int main(int argc, char *argv[]) {
    cag_option_context context;
    cag_option_prepare(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
    while (cag_option_fetch(&context)) {
        char identifier = cag_option_get(&context);
        switch (identifier) {
            case 'h':
                printf("Usage: SFTODOEXENAME [OPTION]... [INPUTFILE] [OUTPUTFILE]\n");
                printf("SFTODO DESCRIPTION.\n\n");
                cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
                return EXIT_SUCCESS;

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

    printf("config.verbose %d\n", config.verbose);
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
    // TODO: If the output is binary we should probably also check an option (--filter again) and refuse to proceed if so. But *maybe* output being stdout will be used to choose a text output option.

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
    make_service_call();
}

// vi: colorcolumn=80
