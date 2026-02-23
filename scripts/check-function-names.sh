#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Certain function names are disallowed due to section name ambiguities
# introduced by -ffunction-sections.
#
# See the comment above TEXT_MAIN in include/asm-generic/vmlinux.lds.h.

objfile="$1"

if [ ! -f "$objfile" ]; then
	echo "usage: $0 <file.o>" >&2
	exit 1
fi

bad_symbols=$(${NM:-nm} "$objfile" | awk '$2 ~ /^[TtWw]$/ {print $3}' | grep -E '^(startup|exit|split|unlikely|hot|unknown)(\.|$)')

if [ -n "$bad_symbols" ]; then
	echo "$bad_symbols" | while read -r sym; do
		echo "$objfile: error: $sym() function name creates ambiguity with -ffunction-sections" >&2
	done
	exit 1
fi

exit 0
