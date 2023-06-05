#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Runs the C-language litmus tests specified on standard input, using up
# to the specified number of CPUs (defaulting to all of them) and placing
# the results in the specified directory (defaulting to the same place
# the litmus test came from).
#
# sh runlitmushist.sh
#
# Run from the Linux kernel tools/memory-model directory.
# This script uses environment variables produced by parseargs.sh.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

. scripts/hwfnseg.sh

T=/tmp/runlitmushist.sh.$$
trap 'rm -rf $T' 0
mkdir $T

if test -d litmus
then
	:
else
	echo Directory \"litmus\" missing, aborting run.
	exit 1
fi

# Prefixes for per-CPU scripts
for ((i=0;i<$LKMM_JOBS;i++))
do
	echo T=$T >> $T/$i.sh
	cat << '___EOF___' >> $T/$i.sh
	runtest () {
		if scripts/runlitmus.sh $1
		then
			if ! grep -q '^Observation ' $LKMM_DESTDIR/$1$2.out
			then
				echo ' !!! Herd failed, no Observation:' $1
			fi
		else
			exitcode=$?
			if test "$exitcode" -eq 124
			then
				exitmsg="timed out"
			elif test "$exitcode" -eq 253
			then
				exitmsg=
			else
				exitmsg="failed, exit code $exitcode"
			fi
			if test -n "$exitmsg"
			then
				echo ' !!! Herd' ${exitmsg}: $1
			fi
		fi
	}
___EOF___
done

awk -v q="'" -v b='\\' '
{
	print "echo `grep " q "^P[0-9]" b "+(" q " " $0 " | tail -1 | sed -e " q "s/^P" b "([0-9]" b "+" b ")(.*$/" b "1/" q "` " $0
}' | sh | sort -k1n |
awk -v dq='"' -v hwfnseg="$hwfnseg" -v ncpu="$LKMM_JOBS" -v t="$T" '
{
	print "if test -z " dq hwfnseg dq " || scripts/simpletest.sh " dq $2 dq
	print "then"
	print "\techo runtest " dq $2 dq " " hwfnseg " >> " t "/" NR % ncpu ".sh";
	print "fi"
}

END {
	for (i = 0; i < ncpu; i++) {
		print "sh " t "/" i ".sh > " t "/" i ".sh.out 2>&1 &";
		close(t "/" i ".sh");
	}
	print "wait";
}' | sh
cat $T/*.sh.out
if grep -q '!!!' $T/*.sh.out
then
	echo ' ---' Summary: 1>&2
	grep '!!!' $T/*.sh.out 1>&2
	nfail="`grep '!!!' $T/*.sh.out | wc -l`"
	echo 'Number of failed herd7 runs (e.g., timeout): ' $nfail 1>&2
	exit 1
else
	echo All runs completed successfully. 1>&2
	exit 0
fi
