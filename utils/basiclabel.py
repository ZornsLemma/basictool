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
    # TODO: Not very happy with this regular expression. We want to be as
    # general as possible with what comes after the optional =, but we would
    # also ideally not be greedy and parse past a colon, unless it's inside
    # a string.
    definition_list = re.findall(r"^(%%[A-Za-z0-9_]+(=.*)?%%:)", line)
    if len(definition_list) == 0:
        return (None, line)
    label = definition_list[0][0][2:][:-3]
    user_content = line[len(definition_list[0][0]):]
    if user_content.strip() == "":
        # If we end up with a line that's blank, the line (and its associated
        # number) might not be present in the final tokenised BASIC.
        user_content = ":"
    return (label, user_content)


# Return a list of consecutive substrings in 'line' such that even-numbered
# elements of the list are BASIC code and odd-numbered elements are string
# literals.
def break_string_at_literals(line):
    in_literal = False
    i = 0
    output = []
    fragment = ""
    while i < len(line):
        c = line[i]
        if c == '"':
            if not in_literal:
                in_literal = not in_literal
                output.append(fragment)
                fragment = ""
                fragment += c
                i += 1
            else:
                if (i + 1) < len(line) and line[i + 1] == '"':
                    # This is an escaped quote within the literal.
                    fragment += '""'
                    i += 2
                else:
                    fragment += c
                    i += 1
                    in_literal = not in_literal
                    output.append(fragment)
                    fragment = ""
        else:
            fragment += c
            i += 1
    if len(fragment) > 0:
        output.append(fragment)
    return output


def strip_rems(user_line_number, user_content):
    user_content = user_content.lstrip()
    if user_content.startswith("REM"):
        # If we end up with a line that's blank, the line (and its associated
        # number) might not be present in the final tokenised BASIC. Our caller
        # will discard the line if it seems safe.
        return ":"

    in_literal = False
    i = 0
    while i < len(user_content):
        c = user_content[i]
        if c == '"':
            if not in_literal:
                in_literal = not in_literal
            else:
                if (i + 1) < len(user_content) and user_content[i + 1] == "'":
                    # This is an escaped quote within the literal.
                    i += 2
                    continue
                else:
                    in_literal = not in_literal
        elif not in_literal and c == ":":
            if user_content[i + 1:].lstrip().startswith("REM"):
                return user_content[:i]
        i += 1
    return user_content


def find_label_reference(line):
    # TODO: Should this be changed to match find_label_definition? If you do
    # %%label=value%% (note no trailing colon) intending to define a label but
    # miss the trailing colon off, this is *not* recognised as a label reference
    # because it doesn't match this regular expression and so is silently left
    # unaltered rather than generating an undefined label error as it would if it
    # were recognised as a label reference.
    reference_list = re.findall(r"%%[A-Za-z0-9_]+%%", line)
    if len(reference_list) == 0:
        return (None, None, None)
    reference = reference_list[0]
    label = reference[2:][:-2]
    start_index = line.index(reference)
    end_index = start_index + len(reference)
    return (label, start_index, end_index)


parser = argparse.ArgumentParser(description='Preprocess text BBC BASIC to allow use of labels instead of line numbers.\n\nUse "%%LABELNAME%%:" at the start of a line to define a label and "%%LABELNAME%%" to refer to a label.')
parser.add_argument("-s", "--start", metavar="N", type=int, default=None, help="start line numbering with line N")
parser.add_argument("-i", "--increment", metavar="N", type=int, default=1, help="increment line numbers in steps of N")
parser.add_argument("-r", "--strip-rems", action="store_true", help="strip REMs (BASIC comments) from the input")
parser.add_argument("input_file", metavar="INFILE", help="text (not tokenised) BBC BASIC program to preprocess")
parser.add_argument("output_file", metavar="OUTFILE", nargs="?", default=None, help="file to write preprocessed output to")
cmd_args = parser.parse_args()

if cmd_args.start is None:
    cmd_args.start = cmd_args.increment

if cmd_args.input_file is None:
    cmd_args.input_file = "-"
if cmd_args.output_file is None:
    cmd_args.output_file = "-"

auto_line_number_increment = cmd_args.increment
if auto_line_number_increment < 1:
    die("--increment argument must be positive, silly!")
next_auto_line_number = cmd_args.start
if next_auto_line_number < 0:
    die("--start argument must be non-negative, silly!")

global internal_line_number

labels = {"INCREMENT": (False, auto_line_number_increment)}

with my_open(cmd_args.input_file, "r") as f:
    program = []
    for internal_line_number, line in enumerate(f.readlines()):
        if len(line) > 0 and line[-1] == '\n':
            line = line[:-1]
        user_line_number, user_content = split_user_line(line)
        if cmd_args.strip_rems:
            user_content = strip_rems(user_line_number, user_content)
            if user_line_number is None and user_content == ":":
                # strip_rems() will turn a full-line REM into a single colon
                # so the line isn't lost. If it doesn't have a line number,
                # it can't be referenced so we can discard it.
                continue
        label_definition, user_content = find_label_definition(user_content)
        if label_definition is not None:
            i = label_definition.find("=")
            if i == -1:
                label_name = label_definition
                label_value = internal_line_number
            else:
                label_name = label_definition[:i]
                label_value = label_definition[i+1:]
            if label_name in labels:
                die_input("redefinition of label '%s'" % label_name)
            labels[label_name] = (i == -1, label_value)
        if user_line_number is None:
            user_line_number = next_auto_line_number
            next_auto_line_number += auto_line_number_increment
        else:
            if user_line_number < next_auto_line_number:
                die_input("user-supplied line number %d is less than next automatic line number %d" % (user_line_number, next_auto_line_number))
            next_auto_line_number = user_line_number + auto_line_number_increment
        program.append((user_line_number, user_content))

# We build up the final output internally so we don't generate anything if an
# error occurs.
output = []
for internal_line_number, (user_line_number, user_content) in enumerate(program):
    user_content_list = break_string_at_literals(user_content)
    output_line = ""
    while len(user_content_list) > 0:
        user_content_fragment = user_content_list.pop(0)
        while True:
            label_reference, start_index, end_index = find_label_reference(user_content_fragment)
            if label_reference is None:
                break
            if label_reference in labels:
                label_is_line_number, label_value = labels[label_reference]
                if label_is_line_number:
                    label_user_line_number = program[label_value][0]
                    label_value = label_user_line_number
            else:
                die_input("unrecognised label '%s'" % label_reference)
            user_content_fragment = user_content_fragment[:start_index] + str(label_value) + user_content_fragment[end_index:]

        output_line += user_content_fragment
        if len(user_content_list) > 0:
            output_line += user_content_list.pop(0)

    output.append("%d%s" % (user_line_number, output_line))

with my_open(cmd_args.output_file, "w") as f:
    print("\n".join(output), file=f)
