#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

# List of exported symbols
ksyms=$($NM $1 | sed -n 's/.*__ksym_marker_\(.*\)/\1/p' | tr A-Z a-z)

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
