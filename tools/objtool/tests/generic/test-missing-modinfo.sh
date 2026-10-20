#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# The module name in .modinfo ends up in the livepatch's klp_object, so an
# object without one cannot be diffed.

. "$(dirname "$0")/../lib.sh"

setup
build_pair no_modinfo.c
run_diff 255

diff_log | grep -q 'modinfo' ||
	fail "expected a complaint about .modinfo, got: $(diff_log | tail -1)"

pass "object without .modinfo rejected"
