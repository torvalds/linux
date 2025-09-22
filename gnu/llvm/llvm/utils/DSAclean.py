#!/usr/bin/env python

# changelog:
# 10/13/2005b: replaced the # in tmp(.#*)* with alphanumeric and _, this will then remove
# nodes such as %tmp.1.i and %tmp._i.3
# 10/13/2005: exntended to remove variables of the form %tmp(.#)* rather than just
#%tmp.#, i.e. it now will remove %tmp.12.3.15 etc, additionally fixed a spelling error in
# the comments
# 10/12/2005: now it only removes nodes and edges for which the label is %tmp.# rather
# than removing all lines for which the lable CONTAINS %tmp.#

from __future__ import print_function

import re
import sys

if len(sys.argv) < 3:
    print("usage is: ./DSAclean <dot_file_to_be_cleaned> <out_put_file>")
    sys.exit(1)
# get a file object
input = open(sys.argv[1], "r")
output = open(sys.argv[2], "w")
# we'll get this one line at a time...while we could just put the whole thing in a string
# it would kill old computers
buffer = input.readline()
while buffer != "":
    if re.compile('label(\s*)=(\s*)"\s%tmp(.\w*)*(\s*)"').search(buffer):
        # skip next line, write neither this line nor the next
        buffer = input.readline()
    else:
        # this isn't a tmp Node, we can write it
        output.write(buffer)
    # prepare for the next iteration
    buffer = input.readline()
input.close()
output.close()
