#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Two kinds of module-owned static branch key are disabled with a warning
# instead of rejected.
#
# A module-local key is normally fatal: late module patching lets the livepatch
# load before the module it depends on, so the unresolved __jump_table entry is
# dereferenced by jump_label_add_module().  test-jump-label-module-key covers
# that rejection.
#
# Tracepoints and pr_debug() generate such keys everywhere, though, and
# refusing them outright would make any function containing a trace_*() call or
# a pr_debug() unpatchable.  So klp diff drops the entry, says so, and carries
# on: the patched code keeps working with that one tracepoint or debug print
# permanently off.
#
# Both halves matter.  A build that fails is a function nobody can patch; an
# entry left in place is the memory corruption the rejection exists to prevent.
#
# The two exemptions are isolated: remove either and this fails.  That the
# entry is then dropped is asserted but not isolated -- making the caller keep
# it anyway produces no output difference here, so that assertion stands as a
# check on the behaviour rather than on the line which produces it.
#
# Covers the same ground as corpus/x86_64/static-call-module-tracepoint and
# pr-debug-unsupported in Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup

# check <key name> <expected warning>
check()
{
	build_pair jump_label.c -DKEY_NAME="$1" -DMODNAME='"klp_testmod"'
	require_input_section __jump_table

	# Accepted, not rejected: this is the whole point.
	run_diff
	assert_diff_log "$2"

	# And the entry is gone, not merely complained about.
	assert_patched target
	assert_no_section __jump_table
}

check __tracepoint_klp_test        'disabling unsupported tracepoint klp_test'
check __UNIQUE_ID_ddebug_klp_test  'disabling unsupported pr_debug'

pass "tracepoint and pr_debug keys disabled with a warning, not rejected"
