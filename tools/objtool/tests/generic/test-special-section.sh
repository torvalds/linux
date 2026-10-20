#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A .kcfi_traps entry belonging to a patched function must be extracted even
# without ANNOTATE_DATA_SPECIAL and with a local label already at offset 0.

. "$(dirname "$0")/../lib.sh"

setup
build_pair special_section.c

in_symbols orig.o | grep -q 'trap_marker' ||
	probe_skip "fixture produced no .kcfi_traps on this arch"

run_diff

assert_patched target
assert_section ".kcfi_traps"

pass ".kcfi_traps extracted despite a local label at offset 0"
