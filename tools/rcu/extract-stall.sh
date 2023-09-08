#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+

usage() {
	echo Extract any RCU CPU stall warnings present in specified file.
	echo Filter out clocksource lines.  Note that preceding-lines excludes the
	echo initial line of the stall warning but trailing-lines includes it.
	echo
	echo Usage: $(basename $0) dmesg-file [ preceding-lines [ trailing-lines ] ]
	echo
	echo Error: $1
}

# Terminate the script, if the argument is missing

if test -f "$1" && test -r "$1"
then
	:
else
	usage "Console log file \"$1\" missing or unreadable."
	exit 1
fi

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

