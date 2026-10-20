#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A cloned __jump_table entry must keep a relocation in its key slot, whether
# the key needs a klp relocation or not.

. "$(dirname "$0")/../lib.sh"

key=klp_test_key

setup
build_pair jump_label.c

has_input_section orig.o __jump_table ||
	probe_skip "fixture produced no __jump_table on this arch"

key_slot_relocs()
{
	out_relocs | awk '/rela__jump_table/,/^$/' | grep -c "^0*8[[:space:]]"
}

# Unexported: klp relocation, key slot holds a tombstone.
export_syms
run_diff

[ "$(key_slot_relocs)" = 1 ] ||
	fail "unexported key: key slot has no relocation"
out_relocs | awk '/rela__jump_table/,/^$/' | grep -q "\.klp\.tombstone\.$key" ||
	fail "unexported key: expected a .klp.tombstone.$key relocation"
out_symbols | grep -q "\.klp\.sym\..*\.$key," ||
	fail "unexported key: no .klp.sym reference for the real relocation"

# Exported: ordinary relocation, no klp machinery.
export_syms "$key"
run_diff

[ "$(key_slot_relocs)" = 1 ] ||
	fail "exported key: key slot has no relocation"
out_relocs | awk '/rela__jump_table/,/^$/' | grep -q "[[:space:]]$key[[:space:]]*+" ||
	fail "exported key: expected a direct relocation to $key"
out_symbols | grep -q '\.klp\.tombstone\.' &&
	fail "exported key: tombstone emitted for an exported symbol"

pass "key slot populated for exported and unexported vmlinux keys"
