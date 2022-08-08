#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Test that the assembler doesn't need -Wa,-gdwarf-5 when presented with DWARF
# v5 input, such as `.file 0` and `md5 0x00`. Should be fixed in GNU binutils
# 2.35.2. https://sourceware.org/bugzilla/show_bug.cgi?id=25611
echo '.file 0 "filename" md5 0x7a0b65214090b6693bd1dc24dd248245' | \
  $* -gdwarf-5 -Wno-unused-command-line-argument -c -x assembler -o /dev/null -
