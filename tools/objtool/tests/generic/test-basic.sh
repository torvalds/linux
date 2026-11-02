#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Only functions whose code changed get cloned into the patch.

. "$(dirname "$0")/../lib.sh"

setup
build_pair basic.c
run_diff

assert_patched     changed
assert_not_patched untouched
assert_section     ".init.klp_funcs"
assert_section     ".init.klp_objects"

pass "changed function cloned, unchanged function left alone"
