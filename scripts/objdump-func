#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Disassemble a single function.
#
# usage: objdump-func <file> <func> [<func> ...]

set -o errexit
set -o nounset

OBJDUMP="${CROSS_COMPILE:-}objdump"

command -v gawk >/dev/null 2>&1 || die "gawk isn't installed"

usage() {
	echo "usage: objdump-func <file> <func> [<func> ...]" >&2
	exit 1
}

[[ $# -lt 2 ]] && usage

OBJ=$1; shift
FUNCS=("$@")

${OBJDUMP} -wdr $OBJ | gawk -M -v _funcs="${FUNCS[*]}" '
	BEGIN { split(_funcs, funcs); }
	/^$/ { func_match=0; }
	/<.*>:/ {
		f = gensub(/.*<(.*)>:/, "\\1", 1);
		for (i in funcs) {
			# match compiler-added suffixes like ".cold", etc
			if (f ~ "^" funcs[i] "(\\..*)?") {
				func_match = 1;
				base = strtonum("0x" $1);
				break;
			}
		}
	}
	{
		if (func_match) {
			addr = strtonum("0x" $1);
			printf("%04x ", addr - base);
			print;
		}
	}'
