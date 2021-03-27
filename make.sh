#!/bin/bash

# TODO: Try building with clang at some point
# TODO: A simple but "real" Makefile to avoid building all in one go every time
gcc -o abe-util -Wall -g --std=c99 src/main.c src/other.c src/lib6502.c src/cargs.c
