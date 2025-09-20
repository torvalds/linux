#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Without the -hw argument, runs a herd7 test and outputs verification
# results to a file whose name is that of the specified litmus test,
# but with ".out" appended.
#
# If the --hw argument is specified, this script translates the .litmus
# C-language file to the specified type of assembly and verifies that.
# But in this case, litmus tests using complex synchronization (such as
# locking, RCU, and SRCU) are cheerfully ignored.
#
# Either way, return the status of the herd7 command.
#
# Usage:
#	runlitmus.sh file.litmus
#
# Run this in the directory containing the memory model, specifying the
# pathname of the litmus test to check.  The caller is expected to have
# properly set up the LKMM environment variables.
#
# Copyright IBM Corporation, 2019
#
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

litmus=$1
if test -f "$litmus" -a -r "$litmus"
then
	:
else
	echo ' !!! ' error: \"$litmus\" is not a readable file
	exit 255
fi

if test -z "$LKMM_HW_MAP_FILE" -o ! -e $LKMM_DESTDIR/$litmus.out
then
	# LKMM run
	herdoptions=${LKMM_HERD_OPTIONS--conf linux-kernel.cfg}
	echo Herd options: $herdoptions > $LKMM_DESTDIR/$litmus.out
	/usr/bin/time $LKMM_TIMEOUT_CMD herd7 $herdoptions $litmus >> $LKMM_DESTDIR/$litmus.out 2>&1
	ret=$?
	if test -z "$LKMM_HW_MAP_FILE"
	then
		exit $ret
	fi
	echo " --- " Automatically generated LKMM output for '"'--hw $LKMM_HW_MAP_FILE'"' run
fi

# Hardware run

T=/tmp/checklitmushw.sh.$$
trap 'rm -rf $T' 0 2
mkdir $T

# Generate filenames
mapfile="Linux2${LKMM_HW_MAP_FILE}.map"
themefile="$T/${LKMM_HW_MAP_FILE}.theme"
herdoptions="-model $LKMM_HW_CAT_FILE"
hwlitmus=`echo $litmus | sed -e 's/\.litmus$/.litmus.'${LKMM_HW_MAP_FILE}'/'`
hwlitmusfile=`echo $hwlitmus | sed -e 's,^.*/,,'`

# Don't run on litmus tests with complex synchronization
if ! scripts/simpletest.sh $litmus
then
	echo ' --- ' error: \"$litmus\" contains locking, RCU, or SRCU
	exit 254
fi

# Generate the assembly code and run herd7 on it.
gen_theme7 -n 10 -map $mapfile -call Linux.call > $themefile
jingle7 -v -theme $themefile $litmus > $LKMM_DESTDIR/$hwlitmus 2> $T/$hwlitmusfile.jingle7.out
if grep -q "Generated 0 tests" $T/$hwlitmusfile.jingle7.out
then
	echo ' !!! ' jingle7 failed, errors in $hwlitmus.err
	cp $T/$hwlitmusfile.jingle7.out $LKMM_DESTDIR/$hwlitmus.err
	exit 253
fi
/usr/bin/time $LKMM_TIMEOUT_CMD herd7 -unroll 0 $LKMM_DESTDIR/$hwlitmus > $LKMM_DESTDIR/$hwlitmus.out 2>&1

exit $?
