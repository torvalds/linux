#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# .klp.symid must not reference symbols in sections the vmlinux link discards.
# Each such section has been its own bug, found only when someone built a
# config where a duplicate happened to land there, so cover the whole list
# rather than whichever one was reported last.

. "$(dirname "$0")/../lib.sh"

setup

# Allocated sections which vmlinux.lds.h discards unconditionally.  A symid
# referencing one of these fails the vmlinux link outright:
#
#   `__exitcall_hid_exit' referenced in section `.klp.symid' of vmlinux.o:
#   defined in discarded section `.exitcall.exit' of vmlinux.o
for sec in .exitcall.exit .no_trim_symbol; do
	build_one symid_discarded.c a.o \
		-DFUNC_NAME=use_a -DDISCARDED_SEC="\"$sec\""
	build_one symid_discarded.c b.o \
		-DFUNC_NAME=use_b -DDISCARDED_SEC="\"$sec\""

	# --klp-symids only runs on a file named vmlinux.o
	rm -f "$workdir/vmlinux.o"
	partial_link "$workdir/vmlinux.o" "$workdir/a.o" "$workdir/b.o" ||
		probe_skip "partial link unavailable"

	"$OBJTOOL" --klp-symids --link "$workdir/vmlinux.o" ||
		fail "objtool --klp-symids failed"

	symids="$(in_relocs vmlinux.o |
		  awk '/rela.klp.symid/,/^$/')"

	# Without this the test would also pass if symid generation stopped
	# entirely.
	echo "$symids" | grep -q 'dup_normal' ||
		fail "$sec: no symid for the duplicate in a live section"

	echo "$symids" | grep -q 'dup_discarded' &&
		fail "symid emitted for a symbol in discarded section $sec"
done

pass "no symids for symbols in discarded sections"
