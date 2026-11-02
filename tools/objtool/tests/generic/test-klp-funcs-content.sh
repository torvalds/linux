#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# .init.klp_funcs is the list the kernel walks to decide what to patch, and
# .init.klp_objects points at it.  Existing tests assert only that the sections
# exist, which they do whether the list names the right functions, the wrong
# ones, or none at all -- and a patch module with an empty function list loads
# perfectly happily and patches nothing.
#
# Each entry pairs a name string in .rodata.klp.str1.1 with a relocation to the
# new function, so both halves are checkable.

. "$(dirname "$0")/../lib.sh"

setup
build_pair klp_funcs.c
run_diff

assert_section .init.klp_funcs
assert_section .init.klp_objects

# Two functions changed, so two entries, each contributing a name relocation
# and a function relocation.
assert_reloc_count .init.klp_funcs 4

# The functions that changed are named ...
assert_reloc_sym .init.klp_funcs first
assert_reloc_sym .init.klp_funcs second
# ... and the one that did not is absent, from the list and from the patch.
assert_no_reloc_sym .init.klp_funcs third
assert_not_patched third

# The names the kernel matches on are real strings, not just relocations.
# readelf prints one per line as "[ offset]  <string>", so compare the whole
# name: a word-boundary match would also accept ".text.first", since a dot is
# not a word character.
for name in first second; do
	out_strings .rodata.klp.str1.1 | awk -v n="$name" '$NF == n' | grep -q . ||
		fail "no '$name' string in .rodata.klp.str1.1"
done

# The object list has to reach the function list, or nothing is walked.
assert_reloc_sym .init.klp_objects .init.klp_funcs

pass "klp_funcs lists exactly the changed functions, by name and relocation"
