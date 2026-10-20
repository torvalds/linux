#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A patch may remove the last ANNOTATE_DATA_SPECIAL in a translation unit while
# leaving the special section it described in place.
#
# klp diff needs entry boundaries for a special section: either an entsize, or
# annotations naming where each entry starts.  .static_call_sites has no
# entsize, so the annotations are all there is -- and when the patched object
# is the only side that lost them, the two sides no longer agree on how the
# section divides up.
#
# The section must still be handled.  Dropping it would leave the patched
# function's static call unregistered; misreading its boundaries would attach
# the entry to the wrong code.  Either way nothing is reported at build time.
#
# Fixed by commit 3de711fba73a ("objtool/klp: Fix create_fake_symbols()
# skipping entsize-based sections").
#
# Covers the same ground as corpus/x86_64/static-call-annotate-stripped in Joe
# Lawrence's klp-build unit test corpus.

. "$(dirname "$0")/../lib.sh"

setup
build_pair static_call.c -DNO_ANNOTATE

# The premise: the original describes its entry, the patched one no longer
# does, and both still have the section itself.
has_input_section orig.o .discard.annotate_data ||
	fail "fixture produced no annotation in the original"
has_input_section patched.o .discard.annotate_data &&
	fail "patched object still has the annotation; nothing was stripped"
assert_input_section .static_call_sites

run_diff

assert_patched target
assert_section .static_call_sites
assert_reloc_sym .static_call_sites target

pass "static call site kept when the patch strips its data annotation"
