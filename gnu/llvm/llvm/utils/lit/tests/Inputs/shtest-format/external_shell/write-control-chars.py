#!/usr/bin/env python

from __future__ import print_function
import sys

print("a line with \x1b[2;30;41mcontrol characters\x1b[0m.")
sys.exit(1)
