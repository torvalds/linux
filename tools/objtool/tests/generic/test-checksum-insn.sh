#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# What a function's checksum has to cover beyond its instruction bytes.
#
# checksum_update_insn() hashes the raw bytes, and then what any relocation on
# the instruction refers to: a string section contributes the string's
# contents, anything else the target symbol's name and the adjusted addend,
# with a reference to a static resolved back through its section symbol first.
#
# None of these show up in the bytes.  A rel32 operand is zero in the object
# and supplied by the relocation, so every change below leaves the encoded
# instruction byte-identical.  A checksum stopping at the bytes reports the
# function unchanged, klp diff omits it, and the patch silently does not
# contain the fix.
#
# test-checksum-position is the other half of this: it covers what must *not*
# change the checksum when a function merely moves.
#
# Covers the same ground as corpus/x86_64/checksum-reloc-sym,
# checksum-pc-relative-addend, checksum-string-reloc and
# checksum-sec-sym-resolve in Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup

# check <flag> <what it changes>
check()
{
	build_pair checksum_insn.c "-D$1"
	run_checksum

	# The premise for all of them: the operand is a relocation, not bytes.
	assert_checksum_differs target
}

check WHICH_CALL    # relocation target name
check STR_CONTENT   # contents of a string the code passes
check WHICH_SLOT    # addend, same target symbol
check WHICH_PRIV    # addend via a static's section symbol

# The converse: rebuilding identical source leaves it alone, so the above is
# not just "any rebuild moves the checksum".
build_pair checksum_insn.c
run_checksum
assert_checksum_matches target

pass "instruction checksums cover reloc targets, addends and string contents"
