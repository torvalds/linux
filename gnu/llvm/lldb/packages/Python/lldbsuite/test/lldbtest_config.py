"""
  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
  See https://llvm.org/LICENSE.txt for license information.
  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

Configuration options for lldbtest.py set by dotest.py during initialization
"""

# array of strings
# each string has the name of an lldb channel followed by
# zero or more categories in that channel
# ex. "gdb-remote packets"
channels = []

# leave logs/traces even for successful test runs
log_success = False

# Indicate whether we're testing with an out-of-tree debugserver
out_of_tree_debugserver = False

# path to the lldb command line executable tool
lldbExec = None

# Environment variables for the inferior
inferior_env = None
