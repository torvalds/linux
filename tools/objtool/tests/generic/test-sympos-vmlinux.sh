#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# sympos for vmlinux, which is resolved differently from sympos for a module.
#
# A module's .ko preserves symbol table order, so klp diff can count -- that is
# what test-sympos covers.  vmlinux cannot be counted: the final link reorders
# sub-sections, so the order in vmlinux.o is not the order the running kernel
# has.  klp diff bridges that with .klp.symid, a table of { id, address }
# emitted into vmlinux.o whose addresses the linker resolves, read back out of
# the linked vmlinux.
#
# Getting it wrong points the relocation at a different symbol of the same
# name.  Nothing fails to build or load; the patched code uses the wrong
# object.
#
# The fixture is arranged so the two answers differ: the static that comes
# first in the symbol table is placed at the *higher* address, so counting
# gives 1 and reading the linked image gives 2.  Without that, both paths agree
# and the test cannot tell them apart.

. "$(dirname "$0")/../lib.sh"

setup

# use_a's static sorts last by section name, use_b's first.  Only use_a is
# patched, so exactly one sympos comes out.
build_one sympos_vmlinux.c orig_a.o    -DFUNC_NAME=use_a -DVARSEC='".data.zzz"'
build_one sympos_vmlinux.c patched_a.o -DFUNC_NAME=use_a -DVARSEC='".data.zzz"' -DPATCHED
build_one sympos_vmlinux.c b.o         -DFUNC_NAME=use_b -DVARSEC='".data.aaa"' -DNO_MODINFO

make_vmlinux_pair "$workdir/orig_a.o" "$workdir/b.o" \
	       -- "$workdir/patched_a.o" "$workdir/b.o"

[ "$(count_input_symbols vmlinux.o dup_counter)" = 2 ] ||
	fail "fixture did not produce two dup_counter symbols"
has_input_section vmlinux.o .klp.symid ||
	fail "objtool --klp-symids emitted no .klp.symid table"
has_input_section vmlinux .klp.symid ||
	fail ".klp.symid did not survive the link"

# The premise: symbol table order and address order must disagree, or the test
# proves nothing.
first_addr="$(in_symbols vmlinux | awk '$8 == "dup_counter" { print $2; exit }')"
low_addr="$(in_symbols vmlinux | awk '$8 == "dup_counter" { print $2 }' | sort | head -1)"
[ "$first_addr" != "$low_addr" ] ||
	probe_skip "linker did not reorder the two statics"

assert_input_symbol dup_counter
run_diff

# Address order says 2.  Counting symbol table order would say 1.
assert_klp_sympos dup_counter 2
out_symbols | grep -q 'dup_counter,1' &&
	fail "sympos 1 emitted: counted symbol table order instead of reading the linked vmlinux"

pass "vmlinux sympos taken from the linked image, not from symbol table order"
