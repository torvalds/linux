#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# sympos is what livepatch uses to tell duplicate symbol names apart in the
# patched object: which "dup_counter" of several the relocation means.  Get it
# wrong and the patch resolves to the wrong object at load time, silently.
#
# klp_find_sympos() reports 0 when a name is unique and a 1-based position when
# it is not, so both need checking -- always reporting a position, or never,
# each looks right in one of the two cases.
#
# This is the module path, counting symbol table order.  vmlinux is reordered
# by the final link and goes through .klp.symid instead; that needs a linked
# vmlinux next to vmlinux.o and is not covered here.

. "$(dirname "$0")/../lib.sh"

setup

# One copy: the name is unique, so there is nothing to disambiguate.
build_one sympos_dup.c orig.o    -DFUNC_NAME=use_a
build_one sympos_dup.c patched.o -DFUNC_NAME=use_a -DPATCHED
run_diff

assert_klp_sympos dup_counter 0

# Two copies: positions, in symbol table order.
for p in "" "-DPATCHED"; do
	# shellcheck disable=SC2086
	build_one sympos_dup.c "a$p.o" -DFUNC_NAME=use_a $p
	# shellcheck disable=SC2086
	build_one sympos_dup.c "b$p.o" -DFUNC_NAME=use_b -DNO_MODINFO $p
done
partial_link "$workdir/orig.o"    "$workdir/a.o"         "$workdir/b.o" ||
	probe_skip "partial link unavailable"
partial_link "$workdir/patched.o" "$workdir/a-DPATCHED.o" "$workdir/b-DPATCHED.o" ||
	probe_skip "partial link unavailable"

# Without duplicates in the input there is nothing for sympos to number.
[ "$(count_input_symbols orig.o dup_counter)" = 2 ] ||
	fail "fixture did not produce two dup_counter symbols"

run_diff

assert_klp_sympos dup_counter 1
assert_klp_sympos dup_counter 2
# ... and nothing still claiming the name is unique
out_symbols | grep -qE '\.klp\.sym\.[^.]+\.dup_counter,0([[:space:]]|$)' &&
	fail "sympos 0 emitted for a duplicated symbol"

pass "sympos numbers duplicate symbols and stays 0 for unique ones"
