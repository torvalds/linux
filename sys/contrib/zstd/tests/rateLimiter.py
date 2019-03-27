#!/usr/bin/env python3

# ################################################################
# Copyright (c) 2018-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ##########################################################################

# Rate limiter, replacement for pv
# this rate limiter does not "catch up" after a blocking period
# Limitations:
# - only accepts limit speed in MB/s

import sys
import time

MB = 1024 * 1024
rate = float(sys.argv[1]) * MB
start = time.time()
total_read = 0

# sys.stderr.close()  # remove error message, for Ctrl+C

try:
  buf = " "
  while len(buf):
    now = time.time()
    to_read = max(int(rate * (now - start)), 1)
    max_buf_size = 1 * MB
    to_read = min(to_read, max_buf_size)
    start = now

    buf = sys.stdin.buffer.read(to_read)
    sys.stdout.buffer.write(buf)

except (KeyboardInterrupt, BrokenPipeError) as e:
    pass
