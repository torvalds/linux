#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A patch may introduce a static call where the original function had none.
#
# The .static_call_sites entry is then new, with nothing in the original to
# correlate it against, so klp diff has to carry it into the patch from
# scratch.  Where the key lives still decides whether that is allowed: a
# vmlinux key is reachable, and a module-owned one is not, for the same reason
# an existing module key is not -- late module patching lets the livepatch load
# first, and the unresolved entry is dereferenced when the module arrives.
#
# Both halves are here because they fail in opposite directions.  Dropping the
# new entry leaves a static call the kernel never patches; accepting a new
# module-owned one is the corruption the check exists to prevent.
#
# Covers the same ground as corpus/x86_64/static-call-vmlinux-new and
# static-call-module-new in Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup
build_pair static_call.c -DNEW_CALL

# The premise: the original really has no static call, the patched one does.
has_input_section orig.o .static_call_sites &&
	fail "fixture put a .static_call_sites in the original; nothing new to add"
has_input_section patched.o .static_call_sites ||
	probe_skip "compiler produced no .static_call_sites on this arch"

run_diff
assert_patched target
assert_section .static_call_sites
assert_reloc_sym .static_call_sites target

# The same new call, with the key owned by a module: not reachable, so the
# build has to stop rather than emit a relocation nothing will resolve.
rm -f "$workdir/out.o"
build_pair static_call.c -DNEW_CALL -DMODNAME='"klp_testmod"'
run_diff 255
assert_diff_log 'unsupported static call key __SCK__klp_test_call'
[ -e "$workdir/out.o" ] &&
	fail "output object produced for a rejected input"

pass "static call introduced by the patch carried in, or rejected for a module key"
