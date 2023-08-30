#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Given a .litmus test and the corresponding litmus output file, check
# the .litmus.out file against the "Result:" comment to judge whether the
# test ran correctly.  If the --hw argument is omitted, check against the
# LKMM output, which is assumed to be in file.litmus.out. If either a
# "DATARACE" marker in the "Result:" comment or a "Flag data-race" marker
# in the LKMM output is present, the other must also be as well, at least
# for litmus tests having a "Result:" comment. In this case, a failure of
# the Always/Sometimes/Never portion of the "Result:" prediction will be
# noted, but forgiven.
#
# If the --hw argument is provided, this is assumed to be a hardware
# test, and the output is assumed to be in file.litmus.HW.out, where
# "HW" is the --hw argument. In addition, non-Sometimes verification
# results will be noted, but forgiven.  Furthermore, if there is no
# "Result:" comment but there is an LKMM .litmus.out file, the observation
# in that file will be used to judge the assembly-language verification.
#
# Usage:
#	judgelitmus.sh file.litmus
#
# Run this in the directory containing the memory model, specifying the
# pathname of the litmus test to check.
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
	litmusout=$litmus.out
	lkmmout=
else
	litmusout="`echo $litmus |
		sed -e 's/\.litmus$/.litmus.'${LKMM_HW_MAP_FILE}'/'`.out"
	lkmmout=$litmus.out
fi
if test -f "$LKMM_DESTDIR/$litmusout" -a -r "$LKMM_DESTDIR/$litmusout"
then
	:
else
	echo ' --- ' error: \"$LKMM_DESTDIR/$litmusout is not a readable file
	exit 255
fi
if grep -q '^Flag data-race$' "$LKMM_DESTDIR/$litmusout"
then
	datarace_modeled=1
fi
if grep -q '^[( ]\* Result: ' $litmus
then
	outcome=`grep -m 1 '^[( ]\* Result: ' $litmus | awk '{ print $3 }'`
	if grep -m1 '^[( ]\* Result: .* DATARACE' $litmus
	then
		datarace_predicted=1
	fi
	if test -n "$datarace_predicted" -a -z "$datarace_modeled" -a -z "$LKMM_HW_MAP_FILE"
	then
		echo '!!! Predicted data race not modeled' $litmus
		exit 252
	elif test -z "$datarace_predicted" -a -n "$datarace_modeled"
	then
		# Note that hardware models currently don't model data races
		echo '!!! Unexpected data race modeled' $litmus
		exit 253
	fi
elif test -n "$LKMM_HW_MAP_FILE" && grep -q '^Observation' $LKMM_DESTDIR/$lkmmout > /dev/null 2>&1
then
	outcome=`grep -m 1 '^Observation ' $LKMM_DESTDIR/$lkmmout | awk '{ print $3 }'`
else
	outcome=specified
fi

grep '^Observation' $LKMM_DESTDIR/$litmusout
if grep -q '^Observation' $LKMM_DESTDIR/$litmusout
then
	:
elif grep ': Unknown macro ' $LKMM_DESTDIR/$litmusout
then
	badname=`grep ': Unknown macro ' $LKMM_DESTDIR/$litmusout |
		sed -e 's/^.*: Unknown macro //' |
		sed -e 's/ (User error).*$//'`
	badmsg=' !!! Current LKMM version does not know "'$badname'"'" $litmus"
	echo $badmsg
	if ! grep -q '!!!' $LKMM_DESTDIR/$litmusout
	then
		echo ' !!! '$badmsg >> $LKMM_DESTDIR/$litmusout 2>&1
	fi
	exit 254
elif grep '^Command exited with non-zero status 124' $LKMM_DESTDIR/$litmusout
then
	echo ' !!! Timeout' $litmus
	if ! grep -q '!!!' $LKMM_DESTDIR/$litmusout
	then
		echo ' !!! Timeout' >> $LKMM_DESTDIR/$litmusout 2>&1
	fi
	exit 124
else
	echo ' !!! Verification error' $litmus
	if ! grep -q '!!!' $LKMM_DESTDIR/$litmusout
	then
		echo ' !!! Verification error' >> $LKMM_DESTDIR/$litmusout 2>&1
	fi
	exit 255
fi
if test "$outcome" = DEADLOCK
then
	if grep '^Observation' $LKMM_DESTDIR/$litmusout | grep -q 'Never 0 0$'
	then
		ret=0
	else
		echo " !!! Unexpected non-$outcome verification" $litmus
		if ! grep -q '!!!' $LKMM_DESTDIR/$litmusout
		then
			echo " !!! Unexpected non-$outcome verification" >> $LKMM_DESTDIR/$litmusout 2>&1
		fi
		ret=1
	fi
elif grep '^Observation' $LKMM_DESTDIR/$litmusout | grep -q 'Never 0 0$'
then
	echo " !!! Unexpected non-$outcome deadlock" $litmus
	if ! grep -q '!!!' $LKMM_DESTDIR/$litmusout
	then
		echo " !!! Unexpected non-$outcome deadlock" $litmus >> $LKMM_DESTDIR/$litmusout 2>&1
	fi
	ret=1
elif grep '^Observation' $LKMM_DESTDIR/$litmusout | grep -q $outcome || test "$outcome" = Maybe
then
	ret=0
else
	if test \( -n "$LKMM_HW_MAP_FILE" -a "$outcome" = Sometimes \) -o -n "$datarace_modeled"
	then
		flag="--- Forgiven"
		ret=0
	else
		flag="!!! Unexpected"
		ret=1
	fi
	echo " $flag non-$outcome verification" $litmus
	if ! grep -qe "$flag" $LKMM_DESTDIR/$litmusout
	then
		echo " $flag non-$outcome verification" >> $LKMM_DESTDIR/$litmusout 2>&1
	fi
fi
tail -2 $LKMM_DESTDIR/$litmusout | head -1
exit $ret
