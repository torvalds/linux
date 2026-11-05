#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A static branch key owned by a module cannot be reached with a klp
# relocation and must be rejected.

. "$(dirname "$0")/../lib.sh"

setup
build_pair jump_label.c -DMODNAME='"klp_testmod"'

has_input_section orig.o __jump_table ||
	probe_skip "fixture produced no __jump_table on this arch"

run_diff 255

diff_log | grep -q 'unsupported static branch key klp_test_key' ||
	fail "expected rejection, got: $(diff_log | tail -1)"
[ -e "$workdir/out.o" ] &&
	fail "output object produced for a rejected input"

pass "module-owned static branch key rejected"
