#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Run herd7 tests on all .litmus files in the litmus-tests directory
# and check each file's result against a "Result:" comment within that
# litmus test.  If the verification result does not match that specified
# in the litmus test, this script prints an error message prefixed with
# "^^^".  It also outputs verification results to a file whose name is
# that of the specified litmus test, but with ".out" appended.
#
# If the --hw argument is specified, this script translates the .litmus
# C-language file to the specified type of assembly and verifies that.
# But in this case, litmus tests using complex synchronization (such as
# locking, RCU, and SRCU) are cheerfully ignored.
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
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

. scripts/parseargs.sh

litmusdir=litmus-tests
if test -d "$litmusdir" -a -r "$litmusdir" -a -x "$litmusdir"
then
	:
else
	echo ' --- ' error: $litmusdir is not an accessible directory
	exit 255
fi

# Create any new directories that have appeared in the litmus-tests
# directory since the last run.
if test "$LKMM_DESTDIR" != "."
then
	find $litmusdir -type d -print |
	( cd "$LKMM_DESTDIR"; sed -e 's/^/mkdir -p /' | sh )
fi

# Run the script on all the litmus tests in the specified directory
ret=0
for i in $litmusdir/*.litmus
do
	if test -n "$LKMM_HW_MAP_FILE" && ! scripts/simpletest.sh $i
	then
		continue
	fi
	if ! scripts/checklitmus.sh $i
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
