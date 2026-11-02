#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A static branch key owned by a module is rejected whether the key is global
# or file-local.
#
# The rejection matters because late module patching allows the livepatch
# module to load before the module it depends on: the __jump_table klp reloc is
# then unresolved, and jump_label_add_module() dereferences an uninitialized
# pointer.  Catching it at build time is the only defence.
#
# test-jump-label-module-key covers the global key.  A file-local one reaches
# the same check by a different route: the compiler emits the reference against
# the section symbol plus an addend, so validate_special_section_klp_reloc()
# has to resolve it to the underlying object before it can see a key at all.
# Until it did, a static key was passed over as "not STT_OBJECT" and the
# unsupported reference was emitted with nothing said.
#
# Fixed by f9fb44b0ecef ("objtool/klp: Fix detection of corrupt static
# branch/call entries").

. "$(dirname "$0")/../lib.sh"

setup
build_pair jump_label.c -DSTATIC_KEY -DMODNAME='"klp_testmod"'

require_input_section __jump_table

# The premise: the key is reached through its section symbol, not by name.
# Without that this is just a second copy of test-jump-label-module-key.
input_jump_relocs="$(in_relocs orig.o | awk '/rela__jump_table/,/^$/')"

echo "$input_jump_relocs" | grep -q klp_test_key ||
	fail "fixture produced no __jump_table reference to the key"
echo "$input_jump_relocs" | grep -qE '\.(bss|data)\.klp_test_key' ||
	probe_skip "compiler referenced the static key by name, not through its section"

run_diff 255

diff_log | grep -q 'unsupported static branch key klp_test_key' ||
	fail "expected rejection, got: $(diff_log | tail -1)"
[ -e "$workdir/out.o" ] &&
	fail "output object produced for a rejected input"

pass "module-owned file-local static branch key rejected"
