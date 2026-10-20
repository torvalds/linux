#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Only the patched function's special section entry may be extracted.

. "$(dirname "$0")/../lib.sh"

setup
build_pair special_section_shared.c

has_input_section orig.o .kcfi_traps ||
	probe_skip "fixture produced no .kcfi_traps on this arch"

run_diff

assert_patched     target
assert_not_patched other
assert_section     ".kcfi_traps"

entries="$(out_relocs | awk '/rela\.kcfi_traps/,/^$/' | grep -c 'target')"
[ "$entries" = 1 ] || fail "expected one .kcfi_traps entry, found $entries"

out_relocs | awk '/rela\.kcfi_traps/,/^$/' | grep -q 'other' &&
	fail "the untouched function's entry was dragged in"

pass "only the patched function's entry extracted"
