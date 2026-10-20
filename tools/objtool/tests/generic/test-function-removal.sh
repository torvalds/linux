#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A patch which deletes a function leaves a symbol in the original with no
# counterpart in the patched object.  klp diff cannot correlate it, and must
# say so and carry on: livepatching cannot remove code from a running kernel,
# so what matters is that the surviving caller is patched and the deleted
# function is not dragged into the patch module.
#
# Cloning it would be worse than useless -- dead code in the patch, plus
# whatever it references, resolved against a kernel where it may not exist.

. "$(dirname "$0")/../lib.sh"

setup
build_pair function_removal.c

# One-sided by construction: present in the original, gone from the patched.
has_input_symbol orig.o going_away ||
	fail "fixture has no going_away in the original"
has_input_symbol patched.o going_away &&
	fail "fixture still has going_away in the patched object"

run_diff

assert_diff_log 'no correlation: going_away'

# The caller changed, so it is patched ...
assert_patched caller
# ... and the deleted function comes along in no form at all.
assert_not_patched going_away
assert_no_symbol going_away

pass "deleted function reported as uncorrelated and left out of the patch"
