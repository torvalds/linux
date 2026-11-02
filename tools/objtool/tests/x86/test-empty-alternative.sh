#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# An x86 alternative with an empty replacement still gets a relocation for its
# replacement offset, but the label it points at is the end of the previous
# replacement -- which is also the start of the next one.  The value is
# meaningless, and get_alt_entry() already ignores it.
#
# Cloning it drags in an unrelated neighboring replacement and everything that
# replacement references.  In the reported case an empty alternative in
# meminfo_proc_show() pulled in one from proc_kcore_init(), emitting a klp
# relocation against init text which is long freed by the time the patch is
# applied.

. "$(dirname "$0")/../lib.sh"

setup
build_pair empty_alternative.c

assert_input_section .altinstructions
assert_input_section .altinstr_replacement

run_diff

# target's own replacement comes along ...
assert_symbol target_repl
# ... neighbor's does not, nor what it references.
assert_no_symbol neighbor_repl
assert_no_symbol neighbor_only

pass "empty alternative's replacement offset ignored when cloning"
