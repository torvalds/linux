#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# find_export() matched on symbol name alone, so a static function or variable
# sharing a name with an export was mistaken for a reference to that export.
# For a vmlinux export that means no klp relocation at all: the normal
# relocation left behind is resolved by the module loader to the vmlinux
# symbol, and the patched code quietly reads and writes the wrong object.
#
# Exports are always global, so a local symbol is never one.

. "$(dirname "$0")/../lib.sh"

setup
build_pair static_local.c

# The static the fixture uses.  Compilers mangle statics variously -- gcc says
# counter.0, clang says target.counter -- so find what this one produced rather
# than assuming a shape.
local_sym="$(in_symbols orig.o |
	awk '$4 == "OBJECT" && $5 == "LOCAL" && $8 ~ /counter/ { print $8; exit }')"
[ -n "$local_sym" ] ||
	fail "fixture produced no local 'counter' symbol"

# Contrive the collision: something else exports that same name.
export_syms "$local_sym" counter
run_diff

# Still treated as the local it is, not as the export.
assert_klp_sym "$local_sym" vmlinux

pass "local symbol not mistaken for an export of the same name"
