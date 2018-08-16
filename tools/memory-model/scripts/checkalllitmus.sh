#!/bin/sh
#
# Run herd tests on all .litmus files in the specified directory (which
# defaults to litmus-tests) and check each file's result against a "Result:"
# comment within that litmus test.  If the verification result does not
# match that specified in the litmus test, this script prints an error
# message prefixed with "^^^".  It also outputs verification results to
# a file whose name is that of the specified litmus test, but with ".out"
# appended.
#
# Usage:
#	checkalllitmus.sh [ directory ]
#
# The LINUX_HERD_OPTIONS environment variable may be used to specify
# arguments to herd, whose default is defined by the checklitmus.sh script.
# Thus, one would normally run this in the directory containing the memory
# model, specifying the pathname of the litmus test to check.
#
# This script makes no attempt to run the litmus tests concurrently.
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

litmusdir=${1-litmus-tests}
if test -d "$litmusdir" -a -r "$litmusdir" -a -x "$litmusdir"
then
	:
else
	echo ' --- ' error: $litmusdir is not an accessible directory
	exit 255
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
for i in litmus-tests/*.litmus
do
	if ! $clscript $i
	then
		ret=1
	fi
done
if test "$ret" -ne 0
then
	echo " ^^^ VERIFICATION MISMATCHES"
else
	echo All litmus tests verified as was expected.
fi
exit $ret
