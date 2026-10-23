#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Link BPF object file(s) with "bpftool gen object" and generate a
# skeleton (or light skeleton) header from the result.
#
# Usage:
#   BPFTOOL=<bpftool> gen_bpf_skel.sh --name NAME --skel OUT
#                      [options] OBJ...
#
#   --name NAME       skeleton object name ("name NAME" for bpftool)
#   --skel OUT        output header ("foo.skel.h" or "foo.lskel.h")
#   --subskel OUT     also generate a subskeleton header into OUT
#   --lskel           generate a light skeleton (bpftool gen skeleton -L)
#   --sign            sign the (light) skeleton, with the key and
#                     certificate taken from $PRIVATE_KEY and
#                     $VERIFICATION_CERT
#
# The inputs are linked three times and the 2nd and 3rd results compared,
# as a regression test for the determinism of "bpftool gen object".
#
# Intermediate files are named after the output header with a "linked"
# infix - "llinked" for light or signed skeletons, so that generating a
# .skel.h and a .lskel.h from the same .bpf.o in parallel never races on
# the intermediates.
#
# The bpftool binary is taken from $BPFTOOL (default: bpftool from PATH).
# On failure all outputs and intermediates are removed and the script exits
# non-zero; permissive-mode skipping is the caller's business (see
# skip_on_fail in the Makefile).

set -u

bpftool=${BPFTOOL:-bpftool}
name='' skel='' subskel=''
lskel=0 sign=0

while [ $# -gt 0 ]; do
	case "$1" in
	--name)		name=$2; shift 2 ;;
	--skel)		skel=$2; shift 2 ;;
	--subskel)	subskel=$2; shift 2 ;;
	--lskel)	lskel=1; shift ;;
	--sign)		sign=1; shift ;;
	--)		shift; break ;;
	-*)		echo "$0: unknown option: $1" >&2; exit 1 ;;
	*)		break ;;
	esac
done

if [ -z "$name" ] || [ -z "$skel" ] || [ $# -eq 0 ]; then
	echo "usage: $0 --name NAME --skel OUT [options] OBJ..." >&2
	exit 1
fi

infix=linked
if [ "$lskel" -eq 1 ] || [ "$sign" -eq 1 ]; then
	infix=llinked
fi

if [ "$sign" -eq 1 ] && { [ -z "${PRIVATE_KEY-}" ] || [ -z "${VERIFICATION_CERT-}" ]; }; then
	echo "$0: --sign requires PRIVATE_KEY and VERIFICATION_CERT in the environment" >&2
	exit 1
fi

base=${skel%.skel.h}
base=${base%.lskel.h}
t1=$base.${infix}1.o
t2=$base.${infix}2.o
t3=$base.${infix}3.o

fail() {
	rm -f "$skel" ${subskel:+"$subskel"} "$t1" "$t2" "$t3"
	exit 1
}

"$bpftool" gen object "$t1" "$@" || fail
"$bpftool" gen object "$t2" "$t1" || fail
"$bpftool" gen object "$t3" "$t2" || fail
if ! cmp -s "$t2" "$t3"; then
	echo "$0: bpftool gen object is not deterministic for $skel" >&2
	fail
fi

args=()
if [ "$sign" -eq 1 ]; then
	args+=(-S -k "$PRIVATE_KEY" -i "$VERIFICATION_CERT")
fi
if [ "$lskel" -eq 1 ]; then
	args+=(-L)
fi
"$bpftool" gen skeleton ${args[@]+"${args[@]}"} "$t3" name "$name" > "$skel" || fail

if [ -n "$subskel" ]; then
	"$bpftool" gen subskeleton "$t3" name "$name" > "$subskel" || fail
fi

rm -f "$t1" "$t2" "$t3"
exit 0
