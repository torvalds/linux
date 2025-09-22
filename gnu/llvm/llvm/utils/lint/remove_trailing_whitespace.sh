#!/bin/sh
# Deletes trailing whitespace in-place in the passed-in files.
# Sample syntax:
#   $0 *.cpp

perl -pi -e 's/\s+$/\n/' $*
