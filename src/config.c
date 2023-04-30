#include "config.h"

// We default to not tokenising the output because it's terminal-friendly.
// This isn't always going to be ideal, but I'm reluctant to say (e.g.)
// "default to tokenising if we're outputting to a file, default to not
// if we're outputting to stdout" because it's potentially confusing.
struct s_config config = {
    0,      // verbose
    false,  // show all output
    -1,     // BASIC version
    false,  // assume input is tokenised
    false,  // strip leading spaces
    false,  // strip trailing spaces
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
    -1,     // LISTO
    false,  // open output as binary
    false,  // format
    false,  // unpack
    false,  // line_ref
    false,  // variable_xref
    false,  // tokenise output
    false,  // ASCII output
};
