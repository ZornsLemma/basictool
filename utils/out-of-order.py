from __future__ import print_function
import argparse
import re
import sys

basictool = "basictool"


def die(s):
    print(s, file=sys.stderr)
    sys.exit(1)


parser = argparse.ArgumentParser(description="Tokenise out-of-order text BBC BASIC")
parser.add_argument("input_file", metavar="INFILE", help="text (not tokenised) BBC BASIC program to process")
parser.add_argument("output_file", metavar="OUTFILE", help="file to write tokenised BASIC to")
cmd_args = parser.parse_args()

with open(cmd_args.input_file, "r") as f:
    for line in f.readlines():
        if line[-1] == "\n":
            line = line[:-1]
        print("X"+line+"X")
        line = line.lstrip()
        line_body_start = re.search("^[0-9]*", line).end()
        if line_body_start == 0:
            die("Missing line number: %s" % line)
