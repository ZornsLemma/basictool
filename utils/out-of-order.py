from __future__ import print_function
import argparse
import re
import six
import subprocess
import sys

basictool = "basictool"


def die(s):
    print(s, file=sys.stderr)
    sys.exit(1)

def run_basictool(args, input_data):
    try:
        child = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = child.communicate(input_data)
    except OSError:
        die("Unable to run basictool; is it on your PATH?")
    if child.returncode != 0:
        print(stderr, file=sys.stderr)
        die("Error running basictool")
    return bytearray(stdout)


parser = argparse.ArgumentParser(description="Renumber out-of-order text BBC BASIC")
parser.add_argument("-t", "--tokenise", action="store_true", help="output tokenised BASIC")
parser.add_argument("input_file", metavar="INFILE", help="text (not tokenised) BBC BASIC program to process")
parser.add_argument("output_file", metavar="OUTFILE", nargs="?", default="-", help="file to write output to (defaults to standard output)")
cmd_args = parser.parse_args()

input_without_line_numbers = []
line_numbers = []
with open(cmd_args.input_file, "r") as f:
    for line in f.readlines():
        if line[-1] == "\n":
            line = line[:-1]
        #print("X"+line+"X")
        line = line.lstrip()
        line_body_start = re.search("^[0-9]*", line).end()
        if line_body_start == 0:
            die("Missing line number: %s" % line)
        line_number = int(line[:line_body_start])
        if line_number > 32767:
            die("Bad line number: %s" % line)
        line_numbers.append(line_number)
        input_without_line_numbers.append(line[line_body_start:])

tokenised = run_basictool([basictool, "-t", "-"], bytearray("\n".join(input_without_line_numbers), "latin-1"))

p = 1
for line_number in line_numbers:
    tokenised[p    ] = line_number >> 8
    tokenised[p + 1] = line_number & 0xff
    p += tokenised[p + 2]
    if tokenised[p] & 0x80 == 0x80:
        break

args = [basictool, "-r"]
if cmd_args.tokenise:
    args.append("-t")
args.append("-")
renumbered = run_basictool(args, tokenised)
if not cmd_args.tokenise:
    renumbered = renumbered.decode("latin-1")
if cmd_args.output_file == "-":
    if cmd_args.tokenise and sys.version_info[0] > 2:
        sys.stdout.buffer.write(renumbered)
    else:
        sys.stdout.write(renumbered)
else:
    with open(cmd_args.output_file, "wb" if cmd_args.tokenise else "w") as f:
        f.write(renumbered)
