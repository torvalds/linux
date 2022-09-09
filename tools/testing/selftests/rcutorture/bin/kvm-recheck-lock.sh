#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Analyze a given results directory for locktorture progress.
#
# Usage: kvm-recheck-lock.sh resdir
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

configfile=`echo $i | sed -e 's/^.*\///'`
ncs=`grep "Writes:  Total:" $i/console.log 2> /dev/null | tail -1 | sed -e 's/^.* Total: //' -e 's/ .*$//'`
if test -z "$ncs"
then
	echo "$configfile -------"
else
	title="$configfile ------- $ncs acquisitions/releases"
	dur=`grep -v '^#' $i/qemu-cmd | sed -e 's/^.* locktorture.shutdown_secs=//' -e 's/ .*$//' 2> /dev/null`
	if test -z "$dur"
	then
		:
	else
		ncsps=`awk -v ncs=$ncs -v dur=$dur '
			BEGIN { print ncs / dur }' < /dev/null`
		title="$title ($ncsps per second)"
	fi
	echo $title
fi
