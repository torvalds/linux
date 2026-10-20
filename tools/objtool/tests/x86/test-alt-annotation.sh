#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A text annotation on an instruction inside an alternative's replacement must
# be carried into the patch.
#
# The kernel annotates replacement code wherever objtool has to be told
# something about it -- a retpoline-safe indirect branch, a deliberately absent
# ENDBR.  Two things made klp diff drop those annotations:
#
#   - replacement code has no real symbol, so objtool invents a NOTYPE fake
#     one, and the extraction only kept references to FUNC symbols;
#   - .discard.annotate_insn was processed before .altinstructions, so the
#     replacement it referenced had no clone to point at yet.
#
# Nothing fails at build time when the annotation goes missing.  It surfaces
# later as objtool warning about, or rejecting, the patched code it was there
# to explain.
#
# Fixed by 62a7a01fde87 ("objtool/klp: Fix extraction of text annotations for
# alternatives").

. "$(dirname "$0")/../lib.sh"

setup
build_pair alt_annotate.c

assert_input_section .altinstructions
assert_input_section .discard.annotate_insn

run_diff

# The annotation has to survive, and to still name the replacement.  Checking
# only the section would pass on an entry whose relocation was dropped.
assert_section .discard.annotate_insn
assert_reloc_sym .discard.annotate_insn target_repl

pass "text annotation on an alternative replacement carried into the patch"
