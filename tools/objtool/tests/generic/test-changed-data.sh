#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Livepatching replaces functions, not data.  A changed data symbol must be
# rejected.

. "$(dirname "$0")/../lib.sh"

setup
build_pair changed_data.c
run_diff 255

diff_log | grep -q 'changed data: klp_test_data' ||
	fail "expected rejection, got: $(diff_log | tail -1)"
[ -e "$workdir/out.o" ] &&
	fail "output object produced for a rejected input"

pass "changed data symbol rejected"
