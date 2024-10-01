#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Analyze a given results directory for rcuscale performance measurements,
# looking for ftrace data.  Exits with 0 if data was found, analyzed, and
# printed.  Intended to be invoked from kvm-recheck-rcuscale.sh after
# argument checking.
#
# Usage: kvm-recheck-rcuscale-ftrace.sh resdir
#
# Copyright (C) IBM Corporation, 2016
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

i="$1"
. functions.sh

if test "`grep -c 'rcu_exp_grace_period.*start' < $i/console.log`" -lt 100
then
	exit 10
fi

sed -e 's/^\[[^]]*]//' < $i/console.log |
grep 'us : rcu_exp_grace_period' |
sed -e 's/us : / : /' |
tr -d '\015' |
awk '
$8 == "start" {
	if (startseq != "")
		nlost++;
	starttask = $1;
	starttime = $3;
	startseq = $7;
	seqtask[startseq] = starttask;
}

$8 == "end" {
	if (startseq == $7) {
		curgpdur = $3 - starttime;
		gptimes[++n] = curgpdur;
		gptaskcnt[starttask]++;
		sum += curgpdur;
		if (curgpdur > 1000)
			print "Long GP " starttime "us to " $3 "us (" curgpdur "us)";
		startseq = "";
	} else {
		# Lost a message or some such, reset.
		startseq = "";
		nlost++;
	}
}

$8 == "done" && seqtask[$7] != $1 {
	piggybackcnt[$1]++;
}

END {
	newNR = asort(gptimes);
	if (newNR <= 0) {
		print "No ftrace records found???"
		exit 10;
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
	print "Distribution of grace periods across tasks:";
	for (i in gptaskcnt) {
		print "\t" i, gptaskcnt[i];
		nbatches += gptaskcnt[i];
	}
	ngps = nbatches;
	print "Distribution of piggybacking across tasks:";
	for (i in piggybackcnt) {
		print "\t" i, piggybackcnt[i];
		ngps += piggybackcnt[i];
	}
	print "Average grace-period duration: " sum / newNR " microseconds";
	print "Minimum grace-period duration: " gptimes[1];
	print "50th percentile grace-period duration: " gptimes[pct50];
	print "90th percentile grace-period duration: " gptimes[pct90];
	print "99th percentile grace-period duration: " gptimes[pct99];
	print "Maximum grace-period duration: " gptimes[newNR];
	print "Grace periods: " ngps + 0 " Batches: " nbatches + 0 " Ratio: " ngps / nbatches " Lost: " nlost + 0;
	print "Computed from ftrace data.";
}'
exit 0
