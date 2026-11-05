#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# "klp checksum --debug-checksum" prints a per-instruction checksum stream,
# and klp-build -f (--show-first-changed) parses it to report where a function
# first differs between the original and patched builds.
#
# It is a debugging aid, so nothing fails when it breaks: klp-build greps the
# stream, and an unmatched grep just yields no output, which reads as "no
# instruction changed".  That is exactly how the format drifted out from under
# it once already.  Pin the shape klp-build depends on:
#
#   DEBUG: <object>: checksum: <func>(): <sym>+0x<offset> <16 hex digits>
#
# and that --dry-run leaves the object alone, since klp-build runs this against
# objects it is going to checksum again for real.

. "$(dirname "$0")/../lib.sh"

setup
build_pair basic.c

before="$(md5sum < "$workdir/orig.o")"

"$OBJTOOL" klp checksum --dry-run --debug-checksum=changed \
	"$workdir/orig.o" > "$workdir/debug.log" 2>&1 ||
	fail "klp checksum --debug-checksum failed"

# --dry-run has to mean it: klp-build checksums these objects again afterwards,
# and "already has .discard.sym_checksum, skipping" would lose the real run.
[ "$(md5sum < "$workdir/orig.o")" = "$before" ] ||
	fail "--dry-run modified the object"
has_input_section orig.o .discard.sym_checksum &&
	fail "--dry-run created .discard.sym_checksum"

grep -qE '^DEBUG: .*: checksum: changed\(\): [^ ]+\+0x[0-9a-f]+ [0-9a-f]{16}$' \
	"$workdir/debug.log" ||
	fail "unexpected --debug-checksum format: $(head -1 "$workdir/debug.log")"

# This is the pattern klp-build greps with.  Keep it working verbatim.
grep -qE "^DEBUG: .*checksum: changed\(\): " "$workdir/debug.log" ||
	fail "klp-build's --show-first-changed pattern no longer matches"

# Only the requested function, or klp-build attributes instructions to the
# wrong one.
grep -qE 'checksum: untouched\(\)' "$workdir/debug.log" &&
	fail "--debug-checksum=changed also dumped untouched()"

pass "--debug-checksum format is the one klp-build -f parses"
