#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Some static locals must not be correlated with their counterparts in the
# running kernel; the patched code has to use a fresh copy instead.
#
# .data..once holds the "have we warned yet" flags behind WARN_ONCE.  Correlate
# one and the patched function inherits the flag from before the patch, so the
# warning the patch was written to produce never fires.  The same goes for the
# names the kernel generates for per-instance state -- __warned, __key,
# __func__ and friends.
#
# Both directions matter, so an ordinary static local is here too: a rule that
# refuses to correlate anything would pass a test that only checks the
# refusals.

. "$(dirname "$0")/../lib.sh"

setup
build_pair static_local_uncorrelated.c
run_diff

# Compilers mangle static locals differently -- gcc gives __key.1, Clang
# target.__key -- so match on the base name.

# Correlated: referenced through a klp symbol, pointing at the kernel's copy.
out_symbols | grep -q '\.klp\.sym\..*ordinary' ||
	fail "ordinary static local was not correlated"

# Not correlated: no klp symbol, and a copy cloned into the patch instead.
out_symbols | grep -q '\.klp\.sym\..*__key' &&
	fail "__key was correlated; it must use a fresh copy"
# .sbss/.sdata on the architectures with a small-data area.
out_sections | grep -qE '\.s?(bss|data)[^ ]*__key' ||
	fail "__key was neither correlated nor cloned"

out_symbols | grep -q '\.klp\.sym\..*once_flag' &&
	fail ".data..once variable was correlated; it must use a fresh copy"
assert_section '.data..once'

pass "per-instance static locals cloned, ordinary ones correlated"
