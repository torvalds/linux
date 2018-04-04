#!/bin/bash
#
# Analyze a given results directory for rcuperf performance measurements.
#
# Usage: kvm-recheck-rcuperf.sh resdir
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
# Copyright (C) IBM Corporation, 2016
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

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

if kvm-recheck-rcuperf-ftrace.sh $i
then
	# ftrace data was successfully analyzed, call it good!
	exit 0
fi

configfile=`echo $i | sed -e 's/^.*\///'`

sed -e 's/^\[[^]]*]//' < $i/console.log |
awk '
/-perf: .* gps: .* batches:/ {
	ngps = $9;
	nbatches = $11;
}

/-perf: .*writer-duration/ {
	gptimes[++n] = $5 / 1000.;
	sum += $5 / 1000.;
}

END {
	newNR = asort(gptimes);
	if (newNR <= 0) {
		print "No rcuperf records found???"
		exit;
	}
	pct50 = int(newNR * 50 / 100);
	if (pct50 < 1)
		pct50 = 1;
	pct90 = int(newNR * 90 / 100);
	if (pct90 < 1)
		pct90 = 1;
	pct99 = int(newNR * 99 / 100);
	if (pct99 < 1)
		pct99 = 1;
	div = 10 ** int(log(gptimes[pct90]) / log(10) + .5) / 100;
	print "Histogram bucket size: " div;
	last = gptimes[1] - 10;
	count = 0;
	for (i = 1; i <= newNR; i++) {
		current = div * int(gptimes[i] / div);
		if (last == current) {
			count++;
		} else {
			if (count > 0)
				print last, count;
			count = 1;
			last = current;
		}
	}
	if (count > 0)
		print last, count;
	print "Average grace-period duration: " sum / newNR " microseconds";
	print "Minimum grace-period duration: " gptimes[1];
	print "50th percentile grace-period duration: " gptimes[pct50];
	print "90th percentile grace-period duration: " gptimes[pct90];
	print "99th percentile grace-period duration: " gptimes[pct99];
	print "Maximum grace-period duration: " gptimes[newNR];
	print "Grace periods: " ngps + 0 " Batches: " nbatches + 0 " Ratio: " ngps / nbatches;
	print "Computed from rcuperf printk output.";
}'
