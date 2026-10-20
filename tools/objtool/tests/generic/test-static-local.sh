#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A static local must be correlated with the original, not duplicated: a second
# copy would discard the state the running kernel accumulated.

. "$(dirname "$0")/../lib.sh"

setup
build_pair static_local.c

in_symbols orig.o | grep -q 'counter' ||
	probe_skip "compiler emitted no distinct static local symbol"

run_diff
assert_patched target

out_symbols | grep -q '\.klp\.sym\..*\.counter' ||
	fail "static local not referenced through a klp relocation"

out_symbols | grep 'counter' | grep -qvE 'UND|\.klp\.(sym|tombstone)' &&
	fail "static local was given a fresh definition"

pass "static local correlated rather than duplicated"
