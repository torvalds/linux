import re
import os
import sys

input_file = open(sys.argv[1])
output_file = open(sys.argv[2], "w")

for line in input_file:
    m = re.search(r"^\s+(clang_[^;]+)", line)
    if m:
        output_file.write(m.group(1) + "\n")
