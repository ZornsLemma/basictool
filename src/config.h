#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "roms.h"

struct s_config {
    int verbose;
    bool show_all_output;
    int basic_version;
    bool input_tokenised;
    // TODO: Rename the next two options strip_spaces_{start,end} to match
    // command-line options? (As part of that, perhaps don't use the words
    // "leading" and "trailing" in comments/other variable names either?)
    bool strip_leading_spaces;
    bool strip_trailing_spaces;
    bool pack;
    bool pack_rems_n;
    bool pack_spaces_n;
    bool pack_comments_n;
    bool pack_variables_n;
    bool pack_singles_n;
    bool pack_concatenate_n;
    bool renumber;
    int renumber_start;
    int renumber_step;
    int listo;
    bool open_output_binary;
    bool format;
    bool unpack;
    bool line_ref;
    bool variable_xref;
    bool output_tokenised;
    bool output_ascii;
};

extern struct s_config config;

#endif
