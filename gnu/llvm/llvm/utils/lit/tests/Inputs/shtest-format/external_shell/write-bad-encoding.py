#!/usr/bin/env python

import sys

getattr(sys.stdout, "buffer", sys.stdout).write(b"a line with bad encoding: \xc2.")
sys.stdout.flush()
