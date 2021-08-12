#!/usr/bin/env python

from __future__ import print_function
import re
import sys


def die(s):
    print(s, file=sys.stderr)
    sys.exit(1)


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



if len(sys.argv) != 2:
    die("Syntax: %s INFILE" % sys.argv[0])

label_internal_line = {}

# TODO: Both of these defaults should be command-line arguments
next_auto_line_number = 0
auto_line_number_increment = 1

with open(sys.argv[1], "r") as f:
    program = []
    for i, line in enumerate(f.readlines()):
        line = line[:-1]
        user_line_number, user_content = split_user_line(line)
        label_definition, user_content = find_label_definition(user_content)
        if label_definition is not None:
            label_internal_line[label_definition] = i
        if user_line_number is None:
            user_line_number = next_auto_line_number
            next_auto_line_number += auto_line_number_increment
        else:
            if user_line_number < next_auto_line_number:
                die("%s:%d:error: user-supplied line number %d is less than next automatic line number %d" % (sys.argv[1], i+1, user_line_number, next_auto_line_number))
            next_auto_line_number = user_line_number + auto_line_number_increment
        program.append((user_line_number, user_content))
        #print (user_line_number, label_definition, user_content) # TODO TEMP

for i, (user_line_number, user_content) in enumerate(program):
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
            die("%s:%d:error: unrecognised label '%s'" % (sys.argv[1], i+1, label_reference))
        user_content = user_content[:start_index] + str(label_value) + user_content[end_index:]
    print("%d%s" % (user_line_number, user_content))
