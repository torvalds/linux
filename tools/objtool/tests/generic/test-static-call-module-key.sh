#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# As for static branches, a static call key owned by a module must be rejected
# while a vmlinux-owned one is accepted.

. "$(dirname "$0")/../lib.sh"

setup
build_pair static_call.c

has_input_section orig.o .static_call_sites ||
	probe_skip "fixture produced no .static_call_sites on this arch"

run_diff
assert_patched target

# The accepted half has to show the entry was carried, not just that the
# function was: dropping the section silently would leave the patched call
# unregistered, and "target was cloned" cannot tell the two apart.
assert_section .static_call_sites
assert_reloc_sym .static_call_sites target

rm -f "$workdir/out.o"
build_pair static_call.c -DMODNAME='"klp_testmod"'
run_diff 255

diff_log | grep -q 'unsupported static call key __SCK__klp_test_call' ||
	fail "expected rejection, got: $(diff_log | tail -1)"

# A rejection has to leave nothing behind.  out.o was removed above, so
# anything here was written by the run which was supposed to refuse.
[ -e "$workdir/out.o" ] &&
	fail "output object produced for a rejected input"

pass "module-owned static call key rejected, vmlinux-owned accepted"
