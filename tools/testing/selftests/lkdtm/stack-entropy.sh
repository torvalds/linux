#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Measure kernel stack entropy by sampling via LKDTM's REPORT_STACK test.
set -e
samples="${1:-1000}"

# Capture dmesg continuously since it may fill up depending on sample size.
log=$(mktemp -t stack-entropy-XXXXXX)
dmesg --follow >"$log" & pid=$!
report=-1
for i in $(seq 1 $samples); do
        echo "REPORT_STACK" >/sys/kernel/debug/provoke-crash/DIRECT
	if [ -t 1 ]; then
		percent=$(( 100 * $i / $samples ))
		if [ "$percent" -ne "$report" ]; then
			/bin/echo -en "$percent%\r"
			report="$percent"
		fi
	fi
done
kill "$pid"

# Count unique offsets since last run.
seen=$(tac "$log" | grep -m1 -B"$samples"0 'Starting stack offset' | \
	grep 'Stack offset' | awk '{print $NF}' | sort | uniq -c | wc -l)
bits=$(echo "obase=2; $seen" | bc | wc -L)
echo "Bits of stack entropy: $bits"
rm -f "$log"

# We would expect any functional stack randomization to be at least 5 bits.
if [ "$bits" -lt 5 ]; then
	exit 1
else
	exit 0
fi
