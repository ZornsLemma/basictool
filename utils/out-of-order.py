from __future__ import print_function
import argparse
import re
import subprocess
import sys

basictool = "basictool"


def die(s):
    print(s, file=sys.stderr)
    sys.exit(1)

def run_basictool(args, input_data):
    child = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = child.communicate(input_data)
    if child.returncode != 0:
        print(stderr, file=sys.stderr)
        die("Error running basictool")
    return stdout


parser = argparse.ArgumentParser(description="Tokenise out-of-order text BBC BASIC")
parser.add_argument("input_file", metavar="INFILE", help="text (not tokenised) BBC BASIC program to process")
parser.add_argument("output_file", metavar="OUTFILE", help="file to write tokenised BASIC to")
cmd_args = parser.parse_args()

input_without_line_numbers = []
with open(cmd_args.input_file, "r") as f:
    for line in f.readlines():
        if line[-1] == "\n":
            line = line[:-1]
        print("X"+line+"X")
        line = line.lstrip()
        line_body_start = re.search("^[0-9]*", line).end()
        if line_body_start == 0:
            die("Missing line number: %s" % line)
        input_without_line_numbers.append(line[line_body_start:])

tokenised = run_basictool([basictool, "-t", "-"], "\n".join(input_without_line_numbers))
with open("temp", "wb") as f:
    f.write(tokenised)
