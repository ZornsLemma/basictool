#include "config.h"

// We default to not tokenising the output because it's terminal-friendly.
// This isn't always going to be ideal, but I'm reluctant to say (e.g.)
// "default to tokenising if we're outputting to a file, default to not
// if we're outputting to stdout" because it's potentially confusing.
struct s_config config = {
    0,      // verbose
    false,  // show all output
    false,  // filter
    false,  // assume input is tokenised
    true,   // strip leading spaces
    true,   // strip trailing spaces
    false,  // pack
    false,  // pack_rems_n
    false,  // pack_spaces_n
    false,  // pack_comments_n
    false,  // pack_variables_n
    false,  // pack_singles_n
    false,  // pack_concatenate_n
    false,  // renumber
    10,     // renumber start
    10,     // renumber step
    0,      // LISTO
    false,  // format
    false,  // line_ref
    false,  // variable_xref
    false,  // tokenise output
    false,  // ASCII output
};
