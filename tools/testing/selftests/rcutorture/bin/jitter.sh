#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Alternate sleeping and spinning on randomly selected CPUs.  The purpose
# of this script is to inflict random OS jitter on a concurrently running
# test.
#
# Usage: jitter.sh me duration [ sleepmax [ spinmax ] ]
#
# me: Random-number-generator seed salt.
# duration: Time to run in seconds.
# sleepmax: Maximum microseconds to sleep, defaults to one second.
# spinmax: Maximum microseconds to spin, defaults to one millisecond.
#
# Copyright (C) IBM Corporation, 2016
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

me=$(($1 * 1000))
duration=$2
sleepmax=${3-1000000}
spinmax=${4-1000}

n=1

starttime=`gawk 'BEGIN { print systime(); }' < /dev/null`

nohotplugcpus=
for i in /sys/devices/system/cpu/cpu[0-9]*
do
	if test -f $i/online
	then
		:
	else
		curcpu=`echo $i | sed -e 's/^[^0-9]*//'`
		nohotplugcpus="$nohotplugcpus $curcpu"
	fi
done

while :
do
	# Check for done.
	t=`gawk -v s=$starttime 'BEGIN { print systime() - s; }' < /dev/null`
	if test "$t" -gt "$duration"
	then
		exit 0;
	fi

	# Set affinity to randomly selected online CPU
	if cpus=`grep 1 /sys/devices/system/cpu/*/online 2>&1 |
		 sed -e 's,/[^/]*$,,' -e 's/^[^0-9]*//'`
	then
		:
	else
		cpus=
	fi
	# Do not leave out non-hot-pluggable CPUs
	cpus="$cpus $nohotplugcpus"

	cpumask=`awk -v cpus="$cpus" -v me=$me -v n=$n 'BEGIN {
		srand(n + me + systime());
		ncpus = split(cpus, ca);
		curcpu = ca[int(rand() * ncpus + 1)];
		mask = lshift(1, curcpu);
		if (mask + 0 <= 0)
			mask = 1;
		printf("%#x\n", mask);
	}' < /dev/null`
	n=$(($n+1))
	if ! taskset -p $cpumask $$ > /dev/null 2>&1
	then
		echo taskset failure: '"taskset -p ' $cpumask $$ '"'
		exit 1
	fi

	# Sleep a random duration
	sleeptime=`awk -v me=$me -v n=$n -v sleepmax=$sleepmax 'BEGIN {
		srand(n + me + systime());
		printf("%06d", int(rand() * sleepmax));
	}' < /dev/null`
	n=$(($n+1))
	sleep .$sleeptime

	# Spin a random duration
	limit=`awk -v me=$me -v n=$n -v spinmax=$spinmax 'BEGIN {
		srand(n + me + systime());
		printf("%06d", int(rand() * spinmax));
	}' < /dev/null`
	n=$(($n+1))
	for i in {1..$limit}
	do
		echo > /dev/null
	done
done

exit 1
