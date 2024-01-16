#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Given a .litmus test and the corresponding .litmus.out file, check
# the .litmus.out file against the "Result:" comment to judge whether
# the test ran correctly.
#
# Usage:
#	judgelitmus.sh file.litmus
#
# Run this in the directory containing the memory model, specifying the
# pathname of the litmus test to check.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

litmus=$1

if test -f "$litmus" -a -r "$litmus"
then
	:
else
	echo ' --- ' error: \"$litmus\" is not a readable file
	exit 255
fi
if test -f "$LKMM_DESTDIR/$litmus".out -a -r "$LKMM_DESTDIR/$litmus".out
then
	:
else
	echo ' --- ' error: \"$LKMM_DESTDIR/$litmus\".out is not a readable file
	exit 255
fi
if grep -q '^ \* Result: ' $litmus
then
	outcome=`grep -m 1 '^ \* Result: ' $litmus | awk '{ print $3 }'`
else
	outcome=specified
fi

grep '^Observation' $LKMM_DESTDIR/$litmus.out
if grep -q '^Observation' $LKMM_DESTDIR/$litmus.out
then
	:
else
	echo ' !!! Verification error' $litmus
	if ! grep -q '!!!' $LKMM_DESTDIR/$litmus.out
	then
		echo ' !!! Verification error' >> $LKMM_DESTDIR/$litmus.out 2>&1
	fi
	exit 255
fi
if test "$outcome" = DEADLOCK
then
	if grep '^Observation' $LKMM_DESTDIR/$litmus.out | grep -q 'Never 0 0$'
	then
		ret=0
	else
		echo " !!! Unexpected non-$outcome verification" $litmus
		if ! grep -q '!!!' $LKMM_DESTDIR/$litmus.out
		then
			echo " !!! Unexpected non-$outcome verification" >> $LKMM_DESTDIR/$litmus.out 2>&1
		fi
		ret=1
	fi
elif grep '^Observation' $LKMM_DESTDIR/$litmus.out | grep -q $outcome || test "$outcome" = Maybe
then
	ret=0
else
	echo " !!! Unexpected non-$outcome verification" $litmus
	if ! grep -q '!!!' $LKMM_DESTDIR/$litmus.out
	then
		echo " !!! Unexpected non-$outcome verification" >> $LKMM_DESTDIR/$litmus.out 2>&1
	fi
	ret=1
fi
tail -2 $LKMM_DESTDIR/$litmus.out | head -1
exit $ret
