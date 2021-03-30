#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

struct s_config {
    int verbose;
    bool filter;
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
    bool tokenise_output;
};

extern struct s_config config;

#endif
