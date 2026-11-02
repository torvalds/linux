#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Data added by the patch has no counterpart in the running kernel and must be
# carried into the livepatch.

. "$(dirname "$0")/../lib.sh"

setup
build_pair new_data.c

# State the premise on both sides.  The array is new in the patched build and
# absent from the original; if the compiler folded it into the code instead of
# emitting it, the assertion below would fail without saying why.
has_input_symbol "$orig_obj" klp_new_data &&
	fail "fixture put klp_new_data in the original; nothing new to carry"
has_input_symbol "$patched_obj" klp_new_data ||
	fail "compiler did not emit klp_new_data; the fixture tests nothing"
in_relocs "$patched_obj" | grep -q 'klp_new_data' ||
	fail "target() does not reference klp_new_data; the fixture tests nothing"

run_diff

assert_patched target
assert_symbol klp_new_data

pass "new data carried into the patch"
