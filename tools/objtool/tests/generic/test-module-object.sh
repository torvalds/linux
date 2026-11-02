#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A klp relocation section is named for the object being patched, not for the
# object which happens to own the symbol being referenced.  Deriving it from
# the symbol means a cross-module reference lands in a section for an object
# the patch may not even touch, so the relocation is never applied and the call
# goes somewhere arbitrary.

. "$(dirname "$0")/../lib.sh"

setup
build_module_pair cross_module.c klp_testmod

# The fixture has to have built as a module for any of this to mean anything.
in_sections orig.o | grep -q '\.modinfo' ||
	fail "fixture has no .modinfo"

# other_mod_func belongs to a different module than the one being patched.
add_exports other_mod other_mod_func
run_diff

# Named for the patched object ...
assert_section __klp_relocs.klp_testmod
# ... not for the object owning the symbol.
assert_no_section __klp_relocs.other_mod

run_post_link
assert_klp_rela klp_testmod .text.target

pass "klp relocation section named for the patched object"
