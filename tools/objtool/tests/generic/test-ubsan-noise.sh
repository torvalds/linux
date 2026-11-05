#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# UBSAN instrumentation in an unchanged function must not make it look changed.
#
# Every instrumented operation gets a per-callsite metadata object in an
# anonymous data section -- .data..Lubsan_data and .data..Lubsan_type from GCC,
# .data..L__unnamed_ from Clang -- whose names are compiler-generated and mean
# nothing across a rebuild.  is_uncorrelated_section() exists so klp diff does
# not try to pair them up.
#
# Without that, the metadata belonging to a function nobody touched compares as
# different and drags the function into the patch.  A livepatch which replaces
# functions the patch never changed is not a build failure: it is a larger
# patch than intended, taking its dependencies with it, and every extra
# function is one more that can fail to correlate or to apply.
#
# Covers the same ground as corpus/x86_64-ubsan/{ubsan-shift-noise,
# ubsan-metadata-data-section,gcc-ubsan-anonymous-data,ubsan-handler-cloning}
# and corpus/x86_64-llvm-ubsan/{clang-ubsan-bounds-noise,
# clang-ubsan-handler-cloning} in Joe Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup
build_pair ubsan_noise.c -fsanitize=shift

# The premise: this compiler really did instrument, and left its metadata in an
# anonymous section.  Without that the test is just test-basic again.
ubsan_sec="$(in_sections orig.o |
	grep -oE '\.data\.\.L(ubsan_data|__unnamed_)[A-Za-z0-9_.]*' | head -1)"
[ -n "$ubsan_sec" ] ||
	probe_skip "compiler emitted no anonymous UBSAN data section"
assert_input_symbol untouched

run_diff

# The changed function is patched, and the untouched one is left alone despite
# carrying instrumentation of its own.
assert_patched touched
assert_not_patched untouched

# The handler the patched code calls has to come with it, or the clone calls
# nothing when its check fires.
out_symbols | grep -q '__ubsan_handle_' ||
	fail "no __ubsan_handle_* reference in the patched output"

pass "UBSAN metadata in an unchanged function does not drag it into the patch"
