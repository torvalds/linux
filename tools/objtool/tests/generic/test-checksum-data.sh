#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# What a data object's checksum has to cover.
#
# checksum_update_object() hashes the symbol's length, its bytes (when the
# section has any -- .bss does not), and then
# every relocation it carries -- as the target's name plus the adjusted addend,
# except for a reference into a string section, which contributes the string's
# contents instead.
#
# Each of those is load-bearing, and the failure is always the same shape: a
# checksum that ignores one of them calls a changed object unchanged, klp diff
# leaves it out of the patch, and the patched code goes on reading the
# kernel's old copy.  Nothing says so at build time.
#
# The string case is the one that cannot be caught by hashing bytes alone.  The
# pointer is identical -- same section, same offset -- and only the text it
# refers to moved.
#
# Covers the same ground as corpus/x86_64/checksum-data-basic,
# checksum-data-func-ptr, checksum-data-string-ptr and checksum-string-reloc in
# Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup

# check <flag> <symbol> <what changed>
#
# Build the pair with one difference and require that symbol's checksum to move.
check()
{
	build_pair checksum_data.c "-D$1"
	run_checksum

	assert_checksum_differs "$2"
}

# The object's own bytes.
check PLAIN_VALUE plain
# Its length, for a .bss object whose bytes are not hashed at all.
check LONGER sized
# A relocation's target: same bytes in the object, different symbol named.
check WHICH_FUNC descriptor
check WHICH_STR descriptor
# The contents of a string the object points at, with the pointer untouched.
check STR_CONTENT descriptor
# A relocation's addend: same target symbol, different offset into it.
check WHICH_SLOT descriptor
# The same, for a static reached through its section symbol: the reference has
# to be resolved back to the object before there is a name or offset to hash.
check WHICH_PRIV descriptor

# Having shown five things that must change it, show one that must not: an
# unrelated edit elsewhere in the file leaves this object alone.
build_pair checksum_data.c -DPLAIN_VALUE
run_checksum
assert_checksum_matches descriptor

pass "data checksums cover length, bytes, reloc targets and string contents"
