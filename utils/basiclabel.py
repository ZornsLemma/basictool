#!/usr/bin/env python

from __future__ import print_function
import argparse
import re
import sys


def die(s):
    print(s, file=sys.stderr)
    sys.exit(1)


def die_input(s):
    die("%s:%d: error: %s" % (cmd_args.input_file, internal_line_number+1, s))


def my_open(name, mode):
    if name == "-":
        if mode == "r":
            return sys.stdin
        elif mode == "w":
            return sys.stdout
        else:
            assert False
    else:
        return open(name, mode)


def split_user_line(line):
    line_number_list = re.findall("^\d+", line)
    if len(line_number_list) == 0:
        return (None, line)
    line_number = line_number_list[0]
    return (int(line_number), line[len(line_number):])


def find_label_definition(line):
    definition_list = re.findall(r"^%%[A-Za-z0-9_]+%%:", line)
    if len(definition_list) == 0:
        return (None, line)
    label = definition_list[0][2:][:-3]
    user_content = line[len(definition_list[0]):]
    if user_content.strip() == "":
        # If we end up with a line that's blank, the line (and its associated
        # number) might not be present in the final tokenised BASIC.
        user_content = ":"
    return (label, user_content)


def find_label_reference(line):
    # TODO: Needs to be smarter about literal strings in line
    reference_list = re.findall(r"%%[A-Za-z0-9_]+%%", line)
    if len(reference_list) == 0:
        return (None, None, None)
    reference = reference_list[0]
    label = reference[2:][:-2]
    start_index = line.index(reference)
    end_index = start_index + len(reference)
    return (label, start_index, end_index)


parser = argparse.ArgumentParser(description="Preprocess text BBC BASIC to allow use of labels instead of line numbers.")
parser.add_argument("-s", "--start", metavar="N", type=int, default=None, help="start line numbering with line N")
parser.add_argument("-i", "--increment", metavar="N", type=int, default=1, help="increment line numbers in steps of N")
parser.add_argument("input_file", metavar="INFILE", help="text BBC BASIC file to process")
parser.add_argument("output_file", metavar="OUTFILE", nargs="?", default=None, help="output filename")
cmd_args = parser.parse_args()

if cmd_args.start is None:
    cmd_args.start = cmd_args.increment

if cmd_args.input_file is None:
    cmd_args.input_file = "-"
if cmd_args.output_file is None:
    cmd_args.output_file = "-"

auto_line_number_increment = cmd_args.increment
if auto_line_number_increment < 1:
    die("--increment argument must be positive")
next_auto_line_number = cmd_args.start
if next_auto_line_number < 0:
    die("--start argument must be non-negative")

global internal_line_number

with my_open(cmd_args.input_file, "r") as f:
    label_internal_line = {}
    program = []
    for internal_line_number, line in enumerate(f.readlines()):
        line = line[:-1]
        user_line_number, user_content = split_user_line(line)
        label_definition, user_content = find_label_definition(user_content)
        if label_definition is not None:
            label_internal_line[label_definition] = internal_line_number
        if user_line_number is None:
            user_line_number = next_auto_line_number
            next_auto_line_number += auto_line_number_increment
        else:
            if user_line_number < next_auto_line_number:
                die_input("user-supplied line number %d is less than next automatic line number %d" % (user_line_number, next_auto_line_number))
            next_auto_line_number = user_line_number + auto_line_number_increment
        program.append((user_line_number, user_content))
        #print (user_line_number, label_definition, user_content) # TODO TEMP

# We build up the final output internally so we don't generate anything if an
# error occurs.
output = []
for internal_line_number, (user_line_number, user_content) in enumerate(program):
    while True:
        label_reference, start_index, end_index = find_label_reference(user_content)
        if label_reference is None:
            break
        if label_reference == "INCREMENT":
            label_value = auto_line_number_increment
        elif label_reference in label_internal_line:
            label_internal_line_number = label_internal_line[label_reference]
            label_user_line_number = program[label_internal_line_number][0]
            label_value = label_user_line_number
        else:
            die_input("unrecognised label '%s'" % label_reference)
        user_content = user_content[:start_index] + str(label_value) + user_content[end_index:]
    output.append("%d%s" % (user_line_number, user_content))

with my_open(cmd_args.output_file, "w") as f:
    print("\n".join(output), file=f)
