#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A patch can change a symbol's linkage without renaming it: dropping "static"
# from a helper so something else can call it, or adding it to one that is no
# longer shared.  Correlation keys off more than the name, so a symbol whose
# binding moved can fail to pair with itself.
#
# Failing to correlate is not a build failure.  The symbol looks new, and a
# "new" data symbol is either rejected or cloned as a second copy -- at which
# point the patched code updates its own private variable and the rest of the
# kernel keeps reading the original.

. "$(dirname "$0")/../lib.sh"

setup
build_pair local_to_global.c

# Confirm the fixture really moved the bindings, in both directions.
#
# Match the binding and the name as fields, not as substrings of the line.
# -ffunction-sections and -fdata-sections give these symbols sections of their
# own, and the section symbols -- .text.flipped_up, .data.flipped_down -- are
# always LOCAL, so "does a LOCAL line mention flipped_up" is answered by the
# wrong symbol.  GNU readelf 2.35 happens to leave section symbol names blank,
# but llvm-readelf prints them, and a premise that holds on one readelf and
# not the other is no premise at all.
has_binding()   # $1 object, $2 binding, $3 symbol
{
	in_symbols "$1" | awk -v b="$2" -v n="$3" '$5 == b && $NF == n' | grep -q .
}

has_binding orig.o    LOCAL  flipped_up ||
	fail "flipped_up is not local in the original"
has_binding patched.o GLOBAL flipped_up ||
	fail "flipped_up is not global in the patched object"
has_binding orig.o    GLOBAL flipped_down ||
	fail "flipped_down is not global in the original"
has_binding patched.o LOCAL  flipped_down ||
	fail "flipped_down is not local in the patched object"

run_diff

assert_diff_log 'changed function: caller'

# Correlated means each pairs with its own counterpart in the original, so the
# patch refers back to the kernel's copy ...
assert_klp_sym flipped_up vmlinux
assert_klp_sym flipped_down vmlinux

# ... rather than carrying its own.  A second copy of flipped_down is the bad
# outcome: patched code would update its private one while the rest of the
# kernel keeps reading the original.
assert_not_patched flipped_up
assert_no_section .data.flipped_down
assert_no_section .bss.flipped_down

diff_log | grep -q 'no correlation' &&
	fail "linkage change reported as an uncorrelated symbol"
diff_log | grep -q 'changed data' &&
	fail "linkage change reported as changed data"

pass "symbols correlated across a change of linkage"
