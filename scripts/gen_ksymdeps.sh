#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

# List of exported symbols
#
# If the object has no symbol, $NM warns 'no symbols'.
# Suppress the stderr.
# TODO:
#   Use -q instead of 2>/dev/null when we upgrade the minimum version of
#   binutils to 2.37, llvm to 13.0.0.
ksyms=$($NM $1 2>/dev/null | sed -n 's/.*__ksym_marker_\(.*\)/\1/p' | tr A-Z a-z)

if [ -z "$ksyms" ]; then
	exit 0
fi

echo
echo "ksymdeps_$1 := \\"

for s in $ksyms
do
	echo $s | sed -e 's:^_*:    $(wildcard include/ksym/:' \
			-e 's:__*:/:g' -e 's/$/.h) \\/'
done

echo
echo "$1: \$(ksymdeps_$1)"
echo
echo "\$(ksymdeps_$1):"
