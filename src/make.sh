#!/bin/bash

# This is a really simple shell script to perform the build. The idea is that
# it may be helpful on non-Unix systems where the makefile doesn't work; it
# probably won't run on those machines, but it should be easy to translate into
# the native equivalent.

# Stop if any errors occur.
set -e
# Show commands as they execute.
set -v

# Generate C-style hex dumps of the ROM images, so they can be compiled into
# the executable. roms.c #includes these auto-generated files.
gcc -o bintoinc -Wall -g --std=c99 bintoinc.c
./bintoinc ../roms/EDITORA100 > zz-editor-a.c
./bintoinc ../roms/EDITORB100 > zz-editor-b.c
./bintoinc ../roms/Basic2 > zz-basic-2.c
./bintoinc ../roms/Basic432 > zz-basic-4.c

gcc -o ../basictool -g -O2 -Wall -Werror --std=c99 main.c config.c emulation.c driver.c roms.c utils.c lib6502.c cargs.c

# vi: colorcolumn=80
