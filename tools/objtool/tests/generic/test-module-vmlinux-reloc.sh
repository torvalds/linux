#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Patching a module, where the patched code references a vmlinux symbol which
# needs a klp relocation.
#
# The kernel does not allow a module-targeted klp relocation to reference a
# vmlinux symbol, and a symbol exported with EXPORT_SYMBOL_FOR_MODULES gets a
# klp relocation.  Put together, filing that relocation under the patched
# module produces a patch the kernel refuses to apply to its target.
#
# So it goes under vmlinux instead, and is applied when the patch module loads
# rather than when the patched module does.  That is the opposite of the rule
# for a reference to a module's symbol, which test-module-object covers; this
# is the other branch of the same decision.

. "$(dirname "$0")/../lib.sh"

setup
# The object being patched is a module ...
build_module_pair cross_module.c klp_testmod

# ... and the symbol it references belongs to vmlinux, exported in a way that
# still requires a klp relocation.
export_syms
add_exports_ns vmlinux module:kvm other_mod_func
run_diff

# Filed against vmlinux, applied when the patch loads.
assert_section __klp_relocs.vmlinux
assert_klp_sym other_mod_func vmlinux

# Not against the patched module: that is the relocation the kernel rejects.
assert_no_section __klp_relocs.klp_testmod

run_post_link
assert_klp_rela vmlinux .text.target
assert_no_section ".klp.rela.klp_testmod..text.target"

pass "klp relocation to a vmlinux symbol filed under vmlinux, not the patched module"
