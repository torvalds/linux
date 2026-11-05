#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# EXPORT_SYMBOL_FOR_MODULES() puts a vmlinux symbol in a "module:<names>"
# namespace, and the module loader grants access by matching the importing
# module's name against that list.  A livepatch module is never on the list, so
# referencing such a symbol with a normal relocation fails modpost, and if that
# is silenced, fails to load with "Unknown symbol".  It needs a klp relocation,
# the same as an unexported symbol.
#
# Ordinary namespaces are not affected: copy_import_ns() propagates the patched
# object's import tags to the patch module, so a normal relocation works.

. "$(dirname "$0")/../lib.sh"

setup
build_pair cross_module.c

sym=other_mod_func

# Plain vmlinux export: a normal relocation is what we want.
export_syms "$sym"
run_diff
assert_no_klp_sym "$sym"

# Ordinary namespace: still a normal relocation.
export_syms
add_exports_ns vmlinux MY_NS "$sym"
run_diff
assert_no_klp_sym "$sym"

# module: namespace: has to become a klp relocation.
export_syms
add_exports_ns vmlinux module:kvm "$sym"
run_diff
assert_klp_sym "$sym" vmlinux
assert_section __klp_relocs.vmlinux

pass "EXPORT_SYMBOL_FOR_MODULES symbol referenced with a klp relocation"
