#!/bin/sh
#
# Run a herd test and check the result against a "Result:" comment within
# the litmus test.  If the verification result does not match that specified
# in the litmus test, this script prints an error message prefixed with
# "^^^" and exits with a non-zero status.  It also outputs verification
# results to a file whose name is that of the specified litmus test, but
# with ".out" appended.
#
# Usage:
#	sh checklitmus.sh file.litmus
#
# The LINUX_HERD_OPTIONS environment variable may be used to specify
# arguments to herd, which default to "-conf linux-kernel.cfg".  Thus,
# one would normally run this in the directory containing the memory model,
# specifying the pathname of the litmus test to check.
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
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

litmus=$1
herdoptions=${LINUX_HERD_OPTIONS--conf linux-kernel.cfg}

if test -f "$litmus" -a -r "$litmus"
then
	:
else
	echo ' --- ' error: \"$litmus\" is not a readable file
	exit 255
fi
if grep -q '^ \* Result: ' $litmus
then
	outcome=`grep -m 1 '^ \* Result: ' $litmus | awk '{ print $3 }'`
else
	outcome=specified
fi

echo Herd options: $herdoptions > $litmus.out
/usr/bin/time herd7 -o ~/tmp $herdoptions $litmus >> $litmus.out 2>&1
grep "Herd options:" $litmus.out
grep '^Observation' $litmus.out
if grep -q '^Observation' $litmus.out
then
	:
else
	cat $litmus.out
	echo ' ^^^ Verification error'
	echo ' ^^^ Verification error' >> $litmus.out 2>&1
	exit 255
fi
if test "$outcome" = DEADLOCK
then
	echo grep 3 and 4
	if grep '^Observation' $litmus.out | grep -q 'Never 0 0$'
	then
		ret=0
	else
		echo " ^^^ Unexpected non-$outcome verification"
		echo " ^^^ Unexpected non-$outcome verification" >> $litmus.out 2>&1
		ret=1
	fi
elif grep '^Observation' $litmus.out | grep -q $outcome || test "$outcome" = Maybe
then
	ret=0
else
	echo " ^^^ Unexpected non-$outcome verification"
	echo " ^^^ Unexpected non-$outcome verification" >> $litmus.out 2>&1
	ret=1
fi
tail -2 $litmus.out | head -1
exit $ret
