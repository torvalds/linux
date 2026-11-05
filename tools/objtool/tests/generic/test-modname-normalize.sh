#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Module.symvers records the build-tree path of the object that exports a
# symbol, not the name the module has at runtime: "arch/x86/kvm/kvm-intel",
# where the kernel knows the module as "kvm_intel".
#
# The klp symbol name embeds the owning object, and livepatch matches it
# against loaded modules by name.  Left unnormalized it names a module that
# does not exist, and the relocation is never resolved -- at load time, with no
# build-time complaint.

. "$(dirname "$0")/../lib.sh"

setup
build_module_pair cross_module.c klp_testmod

# A path with directory components, a dash, and no extension.
export_syms
add_exports "arch/x86/kvm/kvm-intel" other_mod_func
run_diff

# Directories stripped, dash to underscore.
assert_klp_sym other_mod_func kvm_intel

pass "Module.symvers paths normalized to runtime module names"
