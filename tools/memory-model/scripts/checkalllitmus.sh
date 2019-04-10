#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Run herd tests on all .litmus files in the litmus-tests directory
# and check each file's result against a "Result:" comment within that
# litmus test.  If the verification result does not match that specified
# in the litmus test, this script prints an error message prefixed with
# "^^^".  It also outputs verification results to a file whose name is
# that of the specified litmus test, but with ".out" appended.
#
# Usage:
#	checkalllitmus.sh
#
# Run this in the directory containing the memory model.
#
# This script makes no attempt to run the litmus tests concurrently.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

. scripts/parseargs.sh

litmusdir=litmus-tests
if test -d "$litmusdir" -a -r "$litmusdir" -a -x "$litmusdir"
then
	:
else
	echo ' --- ' error: $litmusdir is not an accessible directory
	exit 255
fi

# Create any new directories that have appeared in the github litmus
# repo since the last run.
if test "$LKMM_DESTDIR" != "."
then
	find $litmusdir -type d -print |
	( cd "$LKMM_DESTDIR"; sed -e 's/^/mkdir -p /' | sh )
fi

# Find the checklitmus script.  If it is not where we expect it, then
# assume that the caller has the PATH environment variable set
# appropriately.
if test -x scripts/checklitmus.sh
then
	clscript=scripts/checklitmus.sh
else
	clscript=checklitmus.sh
fi

# Run the script on all the litmus tests in the specified directory
ret=0
for i in $litmusdir/*.litmus
do
	if ! $clscript $i
	then
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
