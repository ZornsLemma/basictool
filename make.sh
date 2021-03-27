#!/bin/bash

set -e

# TODO: Try building with clang at some point
# TODO: A simple but "real" Makefile to avoid building all in one go every time
gcc -o bintoinc -Wall -g --std=c99 src/bintoinc.c
./bintoinc roms/EDITORA100.rom > src/rom-editor-a.c
./bintoinc roms/EDITORB100.rom > src/rom-editor-b.c
./bintoinc roms/basic4.rom > src/rom-basic.c

gcc -o abe-util -Wall -g --std=c99 src/main.c src/other.c src/data.c src/lib6502.c src/cargs.c
