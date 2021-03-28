#include "config.h"

// We default to not tokenising the output because it's terminal-friendly.
// This isn't always going to be ideal, but I'm reluctant to say (e.g.)
// "default to tokenising if we're outputting to a file, default to not
// if we're outputting to stdout" because it's potentially confusing.
struct s_config config = {
    0,      // verbose
    false,  // filter
    true,   // strip leading spaces
    true,   // strip trailing spaces
    false,  // pack
    false,  // tokenise output
};
