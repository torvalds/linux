#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# A malformed Module.symvers has to be reported against the line it is on.
# Module.symvers has tens of thousands of lines and is generated, so a wrong
# line number sends whoever has to fix it to the wrong place, and "line 1" is
# wrong in a way that looks plausible.

. "$(dirname "$0")/../lib.sh"

setup
build_pair basic.c

# Three well-formed lines, then one with no tabs at all.
export_syms a b c
echo 'this line has no fields' >> "$workdir/Module.symvers"

run_diff 255

assert_diff_log 'malformed Module.symvers'
assert_diff_log 'at line 4'

pass "malformed Module.symvers reported against the offending line"
