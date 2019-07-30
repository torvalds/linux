#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Runs the C-language litmus tests matching the specified criteria.
# Generates the output for each .litmus file into a corresponding
# .litmus.out file, and does not judge the result.
#
# sh initlitmushist.sh
#
# Run from the Linux kernel tools/memory-model directory.
# See scripts/parseargs.sh for list of arguments.
#
# This script can consume significant wallclock time and CPU, especially as
# the value of --procs rises.  On a four-core (eight hardware threads)
# 2.5GHz x86 with a one-minute per-run timeout:
#
# --procs wallclock CPU		timeouts	tests
#	1 0m11.241s 0m1.086s           0	   19
#	2 1m12.598s 2m8.459s           2	  393
#	3 1m30.007s 6m2.479s           4	 2291
#	4 3m26.042s 18m5.139s	       9	 3217
#	5 4m26.661s 23m54.128s	      13	 3784
#	6 4m41.900s 26m4.721s         13	 4352
#	7 5m51.463s 35m50.868s        13	 4626
#	8 10m5.235s 68m43.672s        34	 5117
#	9 15m57.80s 105m58.101s       69	 5156
#      10 16m14.13s 103m35.009s       69         5165
#      20 27m48.55s 198m3.286s       156         5269
#
# Increasing the timeout on the 20-process run to five minutes increases
# the runtime to about 90 minutes with the CPU time rising to about
# 10 hours.  On the other hand, it decreases the number of timeouts to 101.
#
# Note that there are historical tests for which herd7 will fail
# completely, for example, litmus/manual/atomic/C-unlock-wait-00.litmus
# contains a call to spin_unlock_wait(), which no longer exists in either
# the kernel or LKMM.

. scripts/parseargs.sh

T=/tmp/initlitmushist.sh.$$
trap 'rm -rf $T' 0
mkdir $T

if test -d litmus
then
	:
else
	git clone https://github.com/paulmckrcu/litmus
	( cd litmus; git checkout origin/master )
fi

# Create any new directories that have appeared in the github litmus
# repo since the last run.
if test "$LKMM_DESTDIR" != "."
then
	find litmus -type d -print |
	( cd "$LKMM_DESTDIR"; sed -e 's/^/mkdir -p /' | sh )
fi

# Create a list of the C-language litmus tests with no more than the
# specified number of processes (per the --procs argument).
find litmus -name '*.litmus' -exec grep -l -m 1 "^C " {} \; > $T/list-C
xargs < $T/list-C -r grep -L "^P${LKMM_PROCS}" > $T/list-C-short

scripts/runlitmushist.sh < $T/list-C-short

exit 0
