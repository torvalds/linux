#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# klp diff extracts entries from eight special sections.  Between them the
# existing tests reach .kcfi_traps, __jump_table, .static_call_sites and
# .altinstructions; __bug_table, __ex_table and __mcount_loc are covered by
# nothing, though the same extraction code serves all of them.
#
# Losing an entry is quiet in every case and wrong in a different way for each:
# a WARN() in patched code that no longer reports where it came from, an
# exception fixup that is simply not there when the faulting instruction traps,
# a function ftrace can no longer see.

. "$(dirname "$0")/../lib.sh"


# section, entry size, relocations per entry
for spec in "__bug_table 12 1" "__ex_table 12 2" "__mcount_loc 8 1"; do
	set -- $spec
	sec=$1

	# A fresh workdir per section: run_diff caches its checksums.
	setup
	build_pair special_sections.c \
		-DSPECIAL_SEC="\"$1\"" -DSPECIAL_ENTSIZE="$2" -DSPECIAL_RELOCS="$3"

	assert_input_section "$sec"
	run_diff

	# Extracted, and pointing at the function that was patched.
	assert_section "$sec"
	assert_reloc_sym "$sec" target
	assert_patched target

	# Nothing belonging to the function that was not.
	assert_not_patched other
	assert_no_reloc_sym "$sec" other

	cleanup
done

pass "__bug_table, __ex_table and __mcount_loc entries extracted"
