#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Two ThinLTO-promoted symbols sharing a demangled name must be paired up
# correctly.
#
# A file-local symbol which ThinLTO has to make visible is renamed
# helper.llvm.<hash>.  With two such helpers in one link the original and the
# patched object hold two each, all four spelled differently, and demangling
# gives "helper" for all of them -- so the name is not enough to say which
# corresponds to which.
#
# Getting it wrong is silent and specific: the patch is built against the wrong
# body, so one call site gets the other helper's arithmetic.  Nothing fails to
# build and nothing fails to load.
#
# test-thinlto-local covers the unambiguous case, one promoted symbol whose
# hash moved.  This is the case where demangling alone is not an answer.
#
# The outcome is asserted, not the machinery: with the clang tested here the
# pairing succeeds even with the .llvm.<hash> suffix map disabled and with
# llvm_suffix() stubbed out, so no single-line sabotage distinguishes it.  The
# tiered matcher this case was written for is not needed for this shape.
#
# Covers the same ground as corpus/x86_64-llvm-thinlto/
# thin-lto-demangled-ambiguity and thin-lto-demangled-global-match in Joe
# Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup
clang_only "ThinLTO requires clang"

find_thinlto_toolchain ||
	probe_skip "no matching clang/lld pair for a ThinLTO link; set THIN_LD to one"

build_thinlto()		# $1 output object, $2 extra flags
{
	local t
	for t in "" -DTU_B -DTU_C; do
		$THIN_CC -flto=thin -O2 -ffunction-sections -fdata-sections \
			$2 $t -c "$FIXTURES_DIR/thinlto_ambiguity.c" \
			-o "$workdir/tu$t.o" 2>/dev/null || return 1
	done
	"$THIN_LD" -r "$workdir/tu.o" "$workdir/tu-DTU_B.o" "$workdir/tu-DTU_C.o" \
		-o "$1" 2>/dev/null || return 1
}

build_thinlto "$workdir/orig.o" "" ||
	probe_skip "ThinLTO build failed ($THIN_CC, $THIN_LD)"
build_thinlto "$workdir/patched.o" -DPATCHED ||
	probe_skip "ThinLTO build failed ($THIN_CC, $THIN_LD)"

# The premise: two promoted helpers per object, and exactly one of them kept
# its hash -- the one the patch did not touch.  Without that there is nothing
# to disambiguate.
orig_syms="$(in_symbols orig.o    | grep -oE 'helper\.llvm\.[0-9]+' | sort -u)"
new_syms="$( in_symbols patched.o | grep -oE 'helper\.llvm\.[0-9]+' | sort -u)"
[ "$(echo "$orig_syms" | wc -l)" = 2 ] && [ "$(echo "$new_syms" | wc -l)" = 2 ] ||
	probe_skip "ThinLTO did not promote two distinct helpers here"

kept="$(comm -12 <(echo "$orig_syms") <(echo "$new_syms"))"
moved="$(comm -13 <(echo "$orig_syms") <(echo "$new_syms"))"
[ "$(echo "$kept" | wc -w)" = 1 ] && [ "$(echo "$moved" | wc -w)" = 1 ] ||
	probe_skip "expected one helper to keep its hash and one to move"

run_diff

# Exactly one helper is cloned, and it is the one whose body changed.  Cloning
# the other, or both, is what a wrong pairing looks like.
assert_not_patched "$kept"

n="$(out_sections | grep -cE '[[:space:]]\.text\.helper\.llvm\.[0-9]+[[:space:]]')"
[ "$n" = 1 ] ||
	fail "expected 1 cloned helper, found $n"

pass "ThinLTO helpers sharing a demangled name paired up correctly"
