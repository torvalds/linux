#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Under kCFI an indirect call checks a type hash before jumping, and traps on a
# mismatch.  Two things belong to the calling function and must come with it
# into a patch:
#
#   - the __cfi_<func> prefix symbol holding the hash.  Lose it and the patched
#     function has no type identity, so indirect calls to it trap.
#   - its .kcfi_traps entry.  Lose that and the trap is not recognised as a
#     CFI failure, so what should be a clean report becomes an oops.
#
# Neither shows up at build time.

. "$(dirname "$0")/../lib.sh"

clang_only "kCFI is a Clang feature"

setup

# Declared above that this is Clang's; a given Clang may still be too old.
cc_supports -fsanitize=kcfi ||
	probe_skip "this clang does not support -fsanitize=kcfi"

build_pair kcfi.c -fsanitize=kcfi

assert_input_section .kcfi_traps
assert_input_symbol __cfi_target

run_diff

assert_patched target

# The prefix symbol comes with its function ...
assert_symbol __cfi_target
# ... and so does the trap entry.
assert_section .kcfi_traps

pass "kCFI prefix symbol and trap entry carried with the patched function"
