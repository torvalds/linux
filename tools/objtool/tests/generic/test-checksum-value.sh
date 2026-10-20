#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# The per-function checksums klp checksum records are what klp diff uses to
# decide which functions changed.  A checksum covering too little misses a real
# change and the patch silently omits the function; one covering too much, or
# unstable across identical input, clones functions nobody patched and drags
# their dependencies in with them.
#
# test-basic covers which functions got cloned, which is downstream of this and
# passes for either kind of wrong checksum as long as the two errors do not
# happen to cancel.  This checks the checksums themselves.

. "$(dirname "$0")/../lib.sh"

setup
build_pair basic.c

assert_input_symbol changed
assert_input_symbol untouched

run_checksum

# The edited function's checksum has to move, the untouched one's must not.
assert_checksum_differs changed
assert_checksum_matches untouched

# And it has to be a function of the code, not of the build: checksumming the
# same input twice has to give the same answer, or every rebuild reports
# spurious changes.
first="$(checksum_of orig.o changed)"
build_pair basic.c
run_checksum
[ "$(checksum_of orig.o changed)" = "$first" ] ||
	fail "checksum for 'changed' differs between builds of identical source"

pass "checksums track the changed function and are stable across rebuilds"
