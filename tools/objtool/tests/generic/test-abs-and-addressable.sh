#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# An absolute symbol and an __ADDRESSABLE() pointer must not disturb the
# function being patched.
#
# A SHN_ABS symbol has no section, so any walk of sym->sec which does not check
# dereferences NULL -- and the kernel has plenty, from linker scripts and from
# .set in assembly.  __ADDRESSABLE() emits a pointer into .discard.addressable
# to keep a symbol referenced; it means nothing to a livepatch and is discarded
# at link time, but it is a relocation like any other and gets looked at.
#
# Neither is what the patch changes.  The failure this guards against is not a
# wrong answer but a crash or an error on input the kernel produces routinely,
# which would make any function near one unpatchable.
#
# Not isolated to a single guard: the absolute symbol here has zero length, so
# it is excluded before the section check is reached and removing that check
# alone changes nothing observable.  This stands as a check on the behaviour
# rather than on the line which produces it.
#
# Covers the same ground as corpus/x86_64/checksum-abs-sym-skip and
# addressable-symbols in Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup
build_pair abs_and_addressable.c

# The premise: the fixture really did produce both.
in_symbols orig.o | grep -q 'ABS.*abs_sym' ||
	probe_skip "assembler did not make abs_sym absolute here"
assert_input_section .discard.addressable

# Checksumming has to survive them, and still see the function that changed.
run_checksum
assert_checksum_differs target
assert_checksum_matches helper

# So does the diff.
run_diff
assert_patched target
assert_not_patched helper

# An absolute symbol has no address to record a checksum against, so it gets
# no entry -- the reference to it is what mattered, not the symbol itself.
in_relocs orig.o | awk '/rela\.discard\.sym_checksum/,/^$/' | grep -qw abs_sym &&
	fail "absolute symbol got a checksum entry"

pass "absolute and __ADDRESSABLE symbols do not disturb the patched function"
