#!/bin/bash

set -e

# TODO: Try building with clang at some point
# TODO: Keep this up to date with Makefile
# TODO: A simple but "real" Makefile to avoid building all in one go every time
gcc -o bintoinc -Wall -g --std=c99 bintoinc.c
./bintoinc ../roms/EDITORA100.rom > rom-editor-a.c
./bintoinc ../roms/EDITORB100.rom > rom-editor-b.c
./bintoinc ../roms/basic4.rom > rom-basic.c

gcc -o ../basictool -Wall -g --std=c99 main.c other.c data.c lib6502.c cargs.c
