#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Run a herd7 test and invokes judgelitmus.sh to check the result against
# a "Result:" comment within the litmus test.  It also outputs verification
# results to a file whose name is that of the specified litmus test, but
# with ".out" appended.
#
# If the --hw argument is specified, this script translates the .litmus
# C-language file to the specified type of assembly and verifies that.
# But in this case, litmus tests using complex synchronization (such as
# locking, RCU, and SRCU) are cheerfully ignored.
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
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

litmus=$1
if test -f "$litmus" -a -r "$litmus"
then
	:
else
	echo ' --- ' error: \"$litmus\" is not a readable file
	exit 255
fi

if test -z "$LKMM_HW_MAP_FILE"
then
	# LKMM run
	herdoptions=${LKMM_HERD_OPTIONS--conf linux-kernel.cfg}
	echo Herd options: $herdoptions > $LKMM_DESTDIR/$litmus.out
	/usr/bin/time $LKMM_TIMEOUT_CMD herd7 $herdoptions $litmus >> $LKMM_DESTDIR/$litmus.out 2>&1
else
	# Hardware run

	T=/tmp/checklitmushw.sh.$$
	trap 'rm -rf $T' 0 2
	mkdir $T

	# Generate filenames
	catfile="`echo $LKMM_HW_MAP_FILE | tr '[A-Z]' '[a-z]'`.cat"
	mapfile="Linux2${LKMM_HW_MAP_FILE}.map"
	themefile="$T/${LKMM_HW_MAP_FILE}.theme"
	herdoptions="-model $LKMM_HW_CAT_FILE"
	hwlitmus=`echo $litmus | sed -e 's/\.litmus$/.'${LKMM_HW_MAP_FILE}'.litmus/'`
	hwlitmusfile=`echo $hwlitmus | sed -e 's,^.*/,,'`

	# Don't run on litmus tests with complex synchronization
	if ! scripts/simpletest.sh $litmus
	then
		echo ' --- ' error: \"$litmus\" contains locking, RCU, or SRCU
		exit 254
	fi

	# Generate the assembly code and run herd7 on it.
	gen_theme7 -n 10 -map $mapfile -call Linux.call > $themefile
	jingle7 -theme $themefile $litmus > $T/$hwlitmusfile 2> $T/$hwlitmusfile.jingle7.out
	/usr/bin/time $LKMM_TIMEOUT_CMD herd7 -model $catfile $T/$hwlitmusfile > $LKMM_DESTDIR/$hwlitmus.out 2>&1
fi

scripts/judgelitmus.sh $litmus
