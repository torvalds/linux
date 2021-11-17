#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Analyze a given results directory for rcutorture progress.
#
# Usage: kvm-recheck-rcu.sh resdir
#
# Copyright (C) IBM Corporation, 2014
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
. functions.sh

configfile=`echo $i | sed -e 's/^.*\///'`
ngps=`grep ver: $i/console.log 2> /dev/null | tail -1 | sed -e 's/^.* ver: //' -e 's/ .*$//'`
stopstate="`grep 'End-test grace-period state: g' $i/console.log 2> /dev/null |
	    tail -1 | sed -e 's/^\[[ 0-9.]*] //' |
	    awk '{ print \"[\" $1 \" \" $5 \" \" $6 \" \" $7 \"]\"; }' |
	    tr -d '\012\015'`"
fwdprog="`grep 'rcu_torture_fwd_prog_cr Duration' $i/console.log 2> /dev/null | sed -e 's/^\[[^]]*] //' | sort -k15nr | head -1 | awk '{ print $14 " " $15 }'`"
if test -z "$ngps"
then
	echo "$configfile ------- " $stopstate
else
	title="$configfile ------- $ngps GPs"
	dur=`grep -v '^#' $i/qemu-cmd | sed -e 's/^.* rcutorture.shutdown_secs=//' -e 's/ .*$//'`
	if test -z "$dur"
	then
		:
	else
		ngpsps=`awk -v ngps=$ngps -v dur=$dur '
			BEGIN { print ngps / dur }' < /dev/null`
		title="$title ($ngpsps/s)"
	fi
	echo $title $stopstate $fwdprog
	nclosecalls=`grep --binary-files=text 'torture: Reader Batch' $i/console.log | tail -1 | \
		awk -v sum=0 '
		{
			for (i = 0; i <= NF; i++) {
				sum += $i;
				if ($i ~ /Batch:/) {
					sum = 0;
					i = i + 2;
				}
			}
		}

		END {
			print sum
		}'`
	if test -z "$nclosecalls"
	then
		exit 0
	fi
	if test "$nclosecalls" -eq 0
	then
		exit 0
	fi
	# Compute number of close calls per tenth of an hour
	nclosecalls10=`awk -v nclosecalls=$nclosecalls -v dur=$dur 'BEGIN { print int(nclosecalls * 36000 / dur) }' < /dev/null`
	if test $nclosecalls10 -gt 5 -a $nclosecalls -gt 1
	then
		print_bug $nclosecalls "Reader Batch close calls in" $(($dur/60)) minute run: $i
	else
		print_warning $nclosecalls "Reader Batch close calls in" $(($dur/60)) minute run: $i
	fi
	echo $nclosecalls "Reader Batch close calls in" $(($dur/60)) minute run: $i > $i/console.log.rcu.diags
fi
