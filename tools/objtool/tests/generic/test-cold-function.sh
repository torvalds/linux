#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Both halves of a split function belong to the patch; carrying only the hot
# part leaves the cold path branching into unpatched code.

. "$(dirname "$0")/../lib.sh"

# Clang does not split functions into a cold part at all, so there is nothing
# for this test to look at there.  A given gcc may or may not split, which is a
# version property rather than a compiler choice -- that stays a probe below.
gcc_only "clang does not split functions into a cold part"

setup

split_flag=-freorder-blocks-and-partition
cc_supports "$split_flag" || split_flag=

build_pair cold_function.c $split_flag

# Find what the compiler called the cold half -- target.cold, target.cold.0,
# depending on version -- and name it exactly from here on.
cold_sym="$(in_symbols orig.o | awk '$NF ~ /^target\.cold/ { print $NF; exit }')"
[ -n "$cold_sym" ] ||
	probe_skip "compiler did not split the function into a cold part"

run_diff

assert_patched target
# Match the name field exactly, and require it to be defined.  Had the cold
# half been left behind, the branch to it would appear as an undefined
# .klp.sym.vmlinux.target.cold,0 -- a different name, which happens to contain
# this one.  Asking about a column instead of the name would not tell them
# apart: readelf prints SHN_LIVEPATCH as "OS [0xff20]" and llvm-readelf as
# "OS[0xff20]", so the fields either side of the name shift between the two.
out_symbols | awk -v n="$cold_sym" '$NF == n && $(NF - 1) != "UND"' | grep -q . ||
	fail "cold half ($cold_sym) was not carried into the patch"

pass "cold half carried into the patch with its parent"
