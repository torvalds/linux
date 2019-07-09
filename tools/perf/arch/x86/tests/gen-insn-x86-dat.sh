#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# gen-insn-x86-dat: generate data for the insn-x86 test
# Copyright (c) 2015, Intel Corporation.
#

set -e

if [ "$(uname -m)" != "x86_64" ]; then
	echo "ERROR: This script only works on x86_64"
	exit 1
fi

cd $(dirname $0)

trap 'echo "Might need a more recent version of binutils"' EXIT

echo "Compiling insn-x86-dat-src.c to 64-bit object"

gcc -g -c insn-x86-dat-src.c

objdump -dSw insn-x86-dat-src.o | awk -f gen-insn-x86-dat.awk > insn-x86-dat-64.c

rm -f insn-x86-dat-src.o

echo "Compiling insn-x86-dat-src.c to 32-bit object"

gcc -g -c -m32 insn-x86-dat-src.c

objdump -dSw insn-x86-dat-src.o | awk -f gen-insn-x86-dat.awk > insn-x86-dat-32.c

rm -f insn-x86-dat-src.o

trap - EXIT

echo "Done (use git diff to see the changes)"
