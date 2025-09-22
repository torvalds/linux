#!/usr/bin/env python

# this is a script to extract given named nodes from a dot file, with
# the associated edges.  An edge is kept iff for edge x -> y
# x and y are both nodes specified to be kept.

# known issues: if a line contains '->' and is not an edge line
# problems will occur.  If node labels do not begin with
# Node this also will not work.  Since this is designed to work
# on DSA dot output and not general dot files this is ok.
# If you want to use this on other files rename the node labels
# to Node[.*] with a script or something.  This also relies on
# the length of a node name being 13 characters (as it is in all
# DSA dot output files)

# Note that the name of the node can be any substring of the actual
# name in the dot file.  Thus if you say specify COLLAPSED
# as a parameter this script will pull out all COLLAPSED
# nodes in the file

# Specifying escape characters in the name like \n also will not work,
# as Python
# will make it \\n, I'm not really sure how to fix this

# currently the script prints the names it is searching for
# to STDOUT, so you can check to see if they are what you intend

from __future__ import print_function

import re
import string
import sys


if len(sys.argv) < 3:
    print(
        "usage is ./DSAextract <dot_file_to_modify> \
			<output_file> [list of nodes to extract]"
    )

# open the input file
input = open(sys.argv[1], "r")

# construct a set of node names
node_name_set = set()
for name in sys.argv[3:]:
    node_name_set |= set([name])

# construct a list of compiled regular expressions from the
# node_name_set
regexp_list = []
for name in node_name_set:
    regexp_list.append(re.compile(name))

# used to see what kind of line we are on
nodeexp = re.compile("Node")
# used to check to see if the current line is an edge line
arrowexp = re.compile("->")

node_set = set()

# read the file one line at a time
buffer = input.readline()
while buffer != "":
    # filter out the unnecessary checks on all the edge lines
    if not arrowexp.search(buffer):
        # check to see if this is a node we are looking for
        for regexp in regexp_list:
            # if this name is for the current node, add the dot variable name
            # for the node (it will be Node(hex number)) to our set of nodes
            if regexp.search(buffer):
                node_set |= set([re.split("\s+", buffer, 2)[1]])
                break
    buffer = input.readline()


# test code
# print '\n'

print(node_name_set)

# print node_set


# open the output file
output = open(sys.argv[2], "w")
# start the second pass over the file
input = open(sys.argv[1], "r")

buffer = input.readline()
while buffer != "":
    # there are three types of lines we are looking for
    # 1) node lines, 2) edge lines 3) support lines (like page size, etc)

    # is this an edge line?
    # note that this is no completely robust, if a none edge line
    # for some reason contains -> it will be missidentified
    # hand edit the file if this happens
    if arrowexp.search(buffer):
        # check to make sure that both nodes are in the node list
        # if they are print this to output
        nodes = arrowexp.split(buffer)
        nodes[0] = string.strip(nodes[0])
        nodes[1] = string.strip(nodes[1])
        if nodes[0][:13] in node_set and nodes[1][:13] in node_set:
            output.write(buffer)
    elif nodeexp.search(buffer):  # this is a node line
        node = re.split("\s+", buffer, 2)[1]
        if node in node_set:
            output.write(buffer)
    else:  # this is a support line
        output.write(buffer)
    buffer = input.readline()
