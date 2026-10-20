#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A Clang switch jump table travels with the function it belongs to.
#
# For a dense enough switch Clang emits the targets as a table in
# .rodata..Lswitch.table.<function>, named after the function but not part of
# it.  klp diff has to associate the two: the patched function indexes into
# that table, so a clone which does not bring it along jumps through whatever
# the kernel's copy holds -- which, when the patch changed the switch, is the
# wrong set of targets.
#
# That is an indirect jump to a stale address, not a missing symbol, so nothing
# reports it at build or load time.
#
# objtool has no switch-specific code: the table is carried by the general
# mechanism for data a cloned function references.  So this is a regression
# test on that mechanism reaching a shape it is easy to get wrong, not a guard
# on a particular line -- making the table uncorrelated, the nearest sabotage,
# does not change the outcome.
#
# Covers the same ground as corpus/x86_64-llvm-switch-rodata/
# clang-switch-rodata-assoc in Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

clang_only "only Clang emits switch jump tables in their own section"

setup
build_pair switch_rodata.c

# The premise: this Clang really did build a table rather than a chain of
# comparisons, and the added case really did change it.
tbl=.rodata..Lswitch.table.status_to_string
has_input_section orig.o "$tbl" ||
	probe_skip "this clang built no jump table for the switch"
# readelf prefixes each line with "[nn]", which splits into one or two fields
# depending on the index, so strip it before counting columns.
tbl_size()
{
	in_sections "$1" | sed 's/^ *\[[ 0-9]*\] *//' |
		awk -v s="$tbl" '$1 == s { print $5 }'
}
[ "$(tbl_size orig.o)" != "$(tbl_size patched.o)" ] ||
	fail "fixture's added case did not change the jump table"

run_diff

assert_patched status_to_string
assert_section "$tbl"
assert_reloc_sym .text.status_to_string "$tbl"

pass "Clang switch jump table carried with the function it belongs to"
