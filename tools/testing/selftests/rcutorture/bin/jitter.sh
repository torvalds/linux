#!/bin/bash
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

me=$(($1 * 1000))
duration=$2
sleepmax=${3-1000000}
spinmax=${4-1000}

n=1

starttime=`awk 'BEGIN { print systime(); }' < /dev/null`

while :
do
	# Check for done.
	t=`awk -v s=$starttime 'BEGIN { print systime() - s; }' < /dev/null`
	if test "$t" -gt "$duration"
	then
		exit 0;
	fi

	# Set affinity to randomly selected CPU
	cpus=`ls /sys/devices/system/cpu/*/online |
		sed -e 's,/[^/]*$,,' -e 's/^[^0-9]*//' |
		grep -v '^0*$'`
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
