#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A patch may introduce a static branch where the original function had none.
#
# That is not the same case as patching a function which already has one.  The
# __jump_table entry is itself new, so there is no counterpart in the original
# to correlate it against: klp diff has to carry the entry and the key into the
# patch from scratch, and the key has to be reached the way any other reference
# to a vmlinux symbol is.
#
# Get it wrong and the entry is dropped, leaving a static branch the kernel
# never patches -- the code takes the wrong arm forever, silently.
#
# Where the key lives still decides whether that is allowed, exactly as it does
# for a key the original already had: a module-owned one cannot be reached, so
# introducing one has to stop the build rather than emit an entry nothing will
# resolve.
#
# Covers the same ground as corpus/x86_64/static-branch-vmlinux-new and
# static-branch-module-new in Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup klp_test_key
build_pair jump_label.c -DNEW_KEY

# The premise: the original really has no jump table, and the patched one does.
has_input_section orig.o __jump_table &&
	fail "fixture put a __jump_table in the original; nothing new to add"
has_input_section patched.o __jump_table ||
	probe_skip "compiler produced no __jump_table on this arch"

run_diff

assert_patched target
assert_section __jump_table
assert_reloc_sym __jump_table target

# The same new branch, with the key owned by a module.  Drop the vmlinux export
# first: while it is exported the key is reachable and being new changes
# nothing, which is what the first version of this got wrong.
export_syms
rm -f "$workdir/out.o"
build_pair jump_label.c -DNEW_KEY -DMODNAME='"klp_testmod"'
run_diff 255
assert_diff_log 'unsupported static branch key klp_test_key'
[ -e "$workdir/out.o" ] &&
	fail "output object produced for a rejected input"

pass "static branch introduced by the patch carried in, or rejected for a module key"
