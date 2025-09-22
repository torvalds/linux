#!/usr/bin/env python3

# ===- bundle_resources.py - Generate string constants with file contents. ===
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===

# Usage: bundle-resources.py foo.inc a.js path/b.css ...
# Produces foo.inc containing:
#   const char a_js[] = "...";
#   const char b_css[] = "...";
import os
import sys

outfile = sys.argv[1]
infiles = sys.argv[2:]

with open(outfile, "w") as out:
    for filename in infiles:
        varname = os.path.basename(filename).replace(".", "_")
        out.write("const char " + varname + "[] = \n")
        # MSVC limits each chunk of string to 2k, so split by lines.
        # The overall limit is 64k, which ought to be enough for anyone.
        for line in open(filename).read().split("\n"):
            out.write('  R"x(' + line + ')x" "\\n"\n')
        out.write("  ;\n")
