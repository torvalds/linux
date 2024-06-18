#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Analyze a given results directory for refscale performance measurements.
#
# Usage: kvm-recheck-refscale.sh resdir
#
# Copyright (C) IBM Corporation, 2016
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

i="$1"
if test -d "$i" -a -r "$i"
then
	:
else
	echo Unreadable results directory: $i
	exit 1
fi
PATH=`pwd`/tools/testing/selftests/rcutorture/bin:$PATH; export PATH
. functions.sh

configfile=`echo $i | sed -e 's/^.*\///'`

sed -e 's/^\[[^]]*]//' < $i/console.log | tr -d '\015' |
awk -v configfile="$configfile" '
/^[ 	]*Runs	Time\(ns\) *$/ {
	if (dataphase + 0 == 0) {
		dataphase = 1;
		# print configfile, $0;
	}
	next;
}

/[^ 	]*[0-9][0-9]*	[0-9][0-9]*\.[0-9][0-9]*$/ {
	if (dataphase == 1) {
		# print $0;
		readertimes[++n] = $2;
		sum += $2;
	}
	next;
}

{
	if (dataphase == 1)
		dataphase == 2;
	next;
}

END {
	print configfile " results:";
	newNR = asort(readertimes);
	if (newNR <= 0) {
		print "No refscale records found???"
		exit;
	}
	medianidx = int(newNR / 2);
	if (newNR == medianidx * 2)
		medianvalue = (readertimes[medianidx - 1] + readertimes[medianidx]) / 2;
	else
		medianvalue = readertimes[medianidx];
	points = "Points:";
	for (i = 1; i <= newNR; i++)
		points = points " " readertimes[i];
	print points;
	print "Average reader duration: " sum / newNR " nanoseconds";
	print "Minimum reader duration: " readertimes[1];
	print "Median reader duration: " medianvalue;
	print "Maximum reader duration: " readertimes[newNR];
	print "Computed from refscale printk output.";
}'
