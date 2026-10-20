#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Without .discard.sym_checksum there is nothing to compare; concluding that
# nothing changed would be worse than failing.

. "$(dirname "$0")/../lib.sh"

setup
build_pair basic.c

( cd "$workdir" && "$OBJTOOL" klp diff orig.o patched.o out.o ) \
	> "$workdir/diff.log" 2>&1 && fail "klp diff accepted an unchecksummed object"

grep -q 'sym_checksum' "$workdir/diff.log" ||
	fail "expected a complaint about the checksum section, got: $(tail -1 "$workdir/diff.log")"

pass "unchecksummed input rejected"
