#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A function's checksum must not depend on where the function sits.
#
# A jump or call without a relocation encodes its target as an offset from the
# instruction.  Hashing those bytes makes the checksum change whenever anything
# ahead of the function changes size -- so an unrelated edit elsewhere in the
# file reports this function as changed too, and the patch grows to include it
# and everything it references.  Nothing fails; the livepatch is just larger and
# riskier than the patch it came from.
#
# Here the "patch" moves target() away from callee() and changes nothing else:
# both are aligned to 64 in the patched build, which shifts them apart without
# touching a byte of either.  See the fixture for why it is done that way.

. "$(dirname "$0")/../lib.sh"

setup

# -fno-function-sections, or each function is at offset 0 of its own section
# and target() never moves.
build_pair checksum_position.c -fno-function-sections

assert_input_symbol target

# The fixture is only meaningful if the displacement target's calls encode
# actually changed, and that is the distance to callee() -- not target's own
# offset.  A compiler which shifted the two by the same amount would move
# target and leave the distance alone, and then the bytes are identical and
# the checksum matches for the uninteresting reason.  Ask about the distance.
sym_off()   # $1 object, $2 symbol
{
	in_symbols "$1" | awk -v n="$2" '$NF == n { print $2; exit }'
}

orig_t="$(sym_off orig.o target)";    orig_c="$(sym_off orig.o callee)"
new_t="$(sym_off patched.o target)";  new_c="$(sym_off patched.o callee)"

[ -n "$orig_t" ] && [ -n "$orig_c" ] && [ -n "$new_t" ] && [ -n "$new_c" ] ||
	fail "target or callee missing from one of the objects"

orig_gap=$(( 16#$orig_t - 16#$orig_c ))
new_gap=$((  16#$new_t  - 16#$new_c ))
[ "$orig_gap" != "$new_gap" ] ||
	probe_skip "this compiler kept target() and callee() the same distance" \
		   "apart; the call displacement did not change"

assert_checksum_matches target

pass "checksum unchanged when the function only moves"
