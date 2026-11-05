#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A function added by the patch has no original to correlate against and must
# still be carried into the livepatch.

. "$(dirname "$0")/../lib.sh"

setup
build_pair new_function.c
run_diff

assert_patched target
assert_symbol klp_new_helper

pass "new function carried into the patch with its caller"
