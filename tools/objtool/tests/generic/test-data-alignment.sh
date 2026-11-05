#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A cloned data section keeps its alignment.
#
# Plenty of kernel data is aligned for correctness rather than speed: per-CPU
# variables, anything touched by an aligned vector move, structures padded to
# own a cacheline. A clone that lands under-aligned either faults on first use
# or silently shares a line it was laid out to avoid, and neither shows up
# until the patch is loaded on hardware that cares.
#
# Fixed by 2f2600decb30 ("objtool/klp: Fix alignment of cloned data
# sections").
#
# Covers the same ground as corpus/x86_64/cloned-data-alignment in Joe
# Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup
build_pair data_alignment.c

# The premise: the compiler really did over-align it, and the object is new in
# the patch so it has to be cloned rather than referenced.
want="$(in_sections patched.o | sed 's/^ *\[[ 0-9]*\] *//' |
	awk '$1 == ".data.aligned_data" { print $NF }')"
[ "$want" = 64 ] ||
	probe_skip "compiler gave .data.aligned_data alignment '$want', not 64"
has_input_section orig.o .data.aligned_data &&
	fail "fixture put aligned_data in the original; nothing to clone"

run_diff
assert_section .data.aligned_data

got="$(out_sections | sed 's/^ *\[[ 0-9]*\] *//' |
       awk '$1 == ".data.aligned_data" { print $NF }')"
[ "$got" = "$want" ] ||
	fail "cloned .data.aligned_data has alignment $got, expected $want"

pass "cloned data section keeps its alignment"
