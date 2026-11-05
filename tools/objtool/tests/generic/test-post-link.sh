#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# klp post-link converts the intermediate __klp_relocs.* sections into the
# .klp.rela.* form the kernel applies at patch load.  Getting this wrong is
# invisible at build time: the module links and loads, and the relocations are
# simply never applied.

. "$(dirname "$0")/../lib.sh"

setup
build_pair static_local.c

# An unexported symbol is what produces a klp relocation in the first place.
# The static local is renamed by the compiler -- counter.0 with gcc -- so the
# fixture's premise is that some local of that shape exists, not that a symbol
# called "counter" does.  Find it first; everything below names it.
local_sym="$(in_symbols orig.o |
	     awk '$4 == "OBJECT" && $5 == "LOCAL" && $8 ~ /counter/ { print $8; exit }')"
[ -n "$local_sym" ] || fail "fixture produced no local 'counter' symbol"

run_diff
assert_section __klp_relocs.vmlinux

# Nothing has converted them yet.
assert_no_section ".klp.rela.vmlinux..text.target"

# The original relocation is neutralised by pointing it at a tombstone, which
# is what stops the module loader resolving it behind livepatch's back.
#
# Compilers mangle a static local differently -- gcc says counter.0, clang
# target.counter -- so find what this one produced rather than assuming.
assert_tombstone "$local_sym"

run_post_link

# One .klp.rela section per base section, carrying SHF_RELA_LIVEPATCH, against
# a symbol in SHN_LIVEPATCH for the kernel to resolve.
assert_klp_rela vmlinux .text.target
assert_livepatch_sym "$local_sym"

pass "klp relocations converted to .klp.rela with SHN_LIVEPATCH symbols"
