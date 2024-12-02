#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Extract any RCU CPU stall warnings present in specified file.
# Filter out clocksource lines.  Note that preceding-lines excludes the
# initial line of the stall warning but trailing-lines includes it.
#
# Usage: extract-stall.sh dmesg-file [ preceding-lines [ trailing-lines ] ]

echo $1
preceding_lines="${2-3}"
trailing_lines="${3-10}"

awk -v preceding_lines="$preceding_lines" -v trailing_lines="$trailing_lines" '
suffix <= 0 {
	for (i = preceding_lines; i > 0; i--)
		last[i] = last[i - 1];
	last[0] = $0;
}

suffix > 0 {
	print $0;
	suffix--;
	if (suffix <= 0)
		print "";
}

suffix <= 0 && /detected stall/ {
	for (i = preceding_lines; i >= 0; i--)
		if (last[i] != "")
			print last[i];
	suffix = trailing_lines;
}' < "$1" | tr -d '\015' | grep -v clocksource

