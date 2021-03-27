#!/bin/bash

# TODO: A simple but "real" Makefile to avoid building all in one go every time
gcc -o abe-util -Wall -g --std=c99 main.c lib6502.c cargs.c
