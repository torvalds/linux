#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Invokes checklitmus.sh on its arguments to run the specified litmus
# test and pass judgment on the results.
#
# Usage:
#	checktheselitmus.sh -- [ file1.litmus [ file2.litmus ... ] ]
#
# Run this in the directory containing the memory model, specifying the
# pathname of the litmus test to check.  The usual parseargs.sh arguments
# can be specified prior to the "--".
#
# This script is intended for use with pathnames that start from the
# tools/memory-model directory.  If some of the pathnames instead start at
# the root directory, they all must do so and the "--destdir /" parseargs.sh
# argument must be specified prior to the "--".  Alternatively, some other
# "--destdir" argument can be supplied as long as the needed subdirectories
# are populated.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

. scripts/parseargs.sh

ret=0
for i in "$@"
do
	if scripts/checklitmus.sh $i
	then
		:
	else
		ret=1
	fi
done
if test "$ret" -ne 0
then
	echo " ^^^ VERIFICATION MISMATCHES" 1>&2
else
	echo All litmus tests verified as was expected. 1>&2
fi
exit $ret
