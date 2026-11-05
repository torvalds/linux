#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Init code and data are freed after boot, so a klp relocation against them can
# never resolve.  Such a patch must be rejected.

. "$(dirname "$0")/../lib.sh"

setup
build_pair init_reference.c

# The rejection can only happen if the patched build really does reference the
# init symbol.  The fixture makes init_only volatile so the read cannot be
# folded away, and this checks that it worked: without a relocation klp diff
# would succeed, and the failure below would read as a missing check rather
# than as a fixture which stopped posing the question.
# Either spelling will do.  A file-local variable is reached through its
# section symbol -- .init.data -- and a global one by name; which of the two
# the compiler picks is its business, and the reference is what matters.
in_relocs "$patched_obj" |
	awk '$5 == ".init.data" || $5 == "init_only"' | grep -q . ||
	fail "patched object has no reference into .init.data; the fixture tests nothing"

run_diff 255

diff_log | grep -q "can't patch or reference init code/data" ||
	fail "expected rejection, got: $(diff_log | tail -1)"

pass "reference to init data rejected"
