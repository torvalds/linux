#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# An alternative's replacement code counts towards the checksum of the function
# it belongs to.
#
# checksum_update_insn() walks insn->alts after hashing the instruction itself:
# the alternative's type, and where the replacement forms a group, its feature
# number and every instruction in it.  So a patch which edits only the
# replacement -- code that runs on some CPUs and not others -- still has to
# move the function's checksum.
#
# If it does not, klp diff decides the function is unchanged and leaves it out.
# The patch then ships the old replacement, and the bug is fixed only on
# machines whose CPU takes the other arm.  Which machines those are depends on
# the feature bit, so the failure looks like a machine-specific bug rather than
# a missing patch.
#
# insn->alts exists only after objtool's check pass, so the pair goes through
# that first -- the compiler emits none of this structure itself.
#
# Covers the same ground as corpus/x86_64/checksum-alt-group,
# checksum-alt-no-group and checksum-alt-recursion-guard in Joe Lawrence's
# klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup

check()
{
	build_pair checksum_alt.c "-D$1"
	assert_input_section .altinstructions
	run_objtool_check --mcount
	run_checksum

	assert_checksum_differs target
}

# The replacement instruction itself.
check ALT_REPL
# The feature number, with no instruction anywhere changed.
check ALT_FEATURE

pass "alternative replacement code counts towards the checksum"
