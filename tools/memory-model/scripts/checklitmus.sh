#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Run a herd test and invokes judgelitmus.sh to check the result against
# a "Result:" comment within the litmus test.  It also outputs verification
# results to a file whose name is that of the specified litmus test, but
# with ".out" appended.
#
# Usage:
#	checklitmus.sh file.litmus
#
# Run this in the directory containing the memory model, specifying the
# pathname of the litmus test to check.  The caller is expected to have
# properly set up the LKMM environment variables.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

litmus=$1
herdoptions=${LKMM_HERD_OPTIONS--conf linux-kernel.cfg}

if test -f "$litmus" -a -r "$litmus"
then
	:
else
	echo ' --- ' error: \"$litmus\" is not a readable file
	exit 255
fi

echo Herd options: $herdoptions > $LKMM_DESTDIR/$litmus.out
/usr/bin/time $LKMM_TIMEOUT_CMD herd7 $herdoptions $litmus >> $LKMM_DESTDIR/$litmus.out 2>&1

scripts/judgelitmus.sh $litmus
