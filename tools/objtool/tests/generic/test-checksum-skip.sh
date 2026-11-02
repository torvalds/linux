#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Symbols which must not get a checksum entry of their own.
#
# .discard.sym_checksum is an array of { address, checksum } looked up by the
# address a relocation points at, so the invariant is one entry per address.
# calculate_checksums() skips three kinds of symbol to keep it:
#
#   zero-length   nothing to hash, and its address belongs to whatever really
#                 lives there
#   alias         a second name for an address already covered
#   cold part     hashed into its parent, which func_for_each_insn() walks
#                 into, so its own entry would double-count
#
# A duplicate entry is not a build failure.  It makes the lookup ambiguous, and
# whichever checksum loses is simply never consulted again -- so a function
# whose code changed can be read as unchanged and dropped from the patch.
#
# Of the three, only the alias skip is isolated here: removing it makes this
# test fail.  A zero-length symbol is excluded by more than one of the guards
# at once -- its section has no data either -- so no single change makes that
# assertion fail, and it stands as a check on the behaviour rather than on the
# line which produces it.  Nothing here reaches the cold-part skip, which
# wants a compiler that splits functions; test-cold-function covers that
# symbol surviving into the patch, not its checksum.
#
# Covers the same ground as corpus/x86_64/checksum-zero-len-sym,
# checksum-alias-skip and checksum-cold-skip in Joe Lawrence's klp-build unit
# test corpus.

. "$(dirname "$0")/../lib.sh"

setup
build_pair checksum_skip.c

assert_input_symbol empty_marker
assert_input_symbol alias_function
run_checksum

# entries_for <object> [-t]
#
# The symbol names .discard.sym_checksum has an entry for, one per line.  With
# -t, what each entry points at instead: the name and its addend, which is the
# address the kernel looks the entry up by.  A name alone is not that address,
# since a relocation against a section symbol names the section and puts the
# offset in the addend, and several entries can then share a name honestly.
entries_for()
{
	in_relocs "$1" | awk -v target="${2:-}" '/rela\.discard\.sym_checksum/,/^$/ {
		if ($1 !~ /^[0-9a-f]{8,}/)
			next
		if (target == "-t")
			print $5, $6, $7
		else
			print $5
	}'
}

entries="$(entries_for orig.o)"

# The control: something real did get an entry, so an empty listing cannot
# make the rest of this pass by default.
echo "$entries" | grep -qx target ||
	fail "no checksum entry for target"

echo "$entries" | grep -qx empty_marker &&
	fail "zero-length symbol got a checksum entry"

# One of the two names for that address is kept and the other skipped; which
# one falls out of symbol table order and is not the point.  Two would be.
n="$(echo "$entries" | grep -cxE 'real_function|alias_function')"
[ "$n" = 1 ] ||
	fail "expected 1 checksum entry across real_function and its alias, found $n"

# One entry per address, which is what the skipping is for.
dupes="$(entries_for orig.o -t | sort | uniq -d)"
[ -z "$dupes" ] ||
	fail "two checksum entries for one address: $dupes"

pass "zero-length symbols and aliases get no checksum entry of their own"
