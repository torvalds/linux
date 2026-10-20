#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# objtool's static call handling must accept a livepatch module which cannot
# see a static call's key symbol.
#
# __SCK__* keys are not exported; modules get read-only access at load time
# instead.  Livepatch modules built by klp-build do have full access to their
# keys, and a check was added on the strength of that -- but a livepatch module
# can also be written by hand, and samples/livepatch is full of them.  One of
# those needs a key it cannot see as soon as it does anything that expands to a
# static call, which with CONFIG_MEM_ALLOC_PROFILING_DEBUG includes allocating
# memory:
#
#   samples/livepatch/livepatch-shadow-fix1.o: error: objtool: static_call:
#     can't find static_call_key symbol: __SCK__WARN_trap
#
# The module built without the livepatch tag is the control: it takes the same
# path and has always been accepted, so a test which only built the livepatch
# one could not tell this fix from the check being removed altogether.
#
# Fixed by f495054bd12e ("objtool/klp: Fix unexported static call key access
# for manually built livepatch modules").

. "$(dirname "$0")/../lib.sh"

setup

# Not a klp subcommand: this is objtool's ordinary check pass, which is what
# runs over a hand-built livepatch module during a normal kernel build.
for tag in "" -DLIVEPATCH; do
	build_one static_call_no_key.c mod.o $tag

	"$OBJTOOL" --module --static-call "$workdir/mod.o" \
		> "$workdir/objtool.log" 2>&1 ||
		fail "objtool rejected a ${tag:+livepatch }module which cannot" \
		     "see its static call key: $(tail -1 "$workdir/objtool.log")"
done

pass "livepatch module accepted without access to its static call key"
