#!/bin/bash
#
# Check the console output from an rcutorture run for oopses.
# The "file" is a pathname on the local system, and "title" is
# a text string for error-message purposes.
#
# Usage: parse-console.sh file title
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
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

file="$1"
title="$2"

. functions.sh

if grep -Pq '\x00' < $file
then
	print_warning Console output contains nul bytes, old qemu still running?
fi
egrep 'Badness|WARNING:|Warn|BUG|===========|Call Trace:|Oops:|detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state' < $file | grep -v 'ODEBUG: ' | grep -v 'Warning: unable to open an initial console' > $1.diags
if test -s $1.diags
then
	print_warning Assertion failure in $file $title
	# cat $1.diags
	summary=""
	n_badness=`grep -c Badness $1`
	if test "$n_badness" -ne 0
	then
		summary="$summary  Badness: $n_badness"
	fi
	n_warn=`grep -v 'Warning: unable to open an initial console' $1 | egrep -c 'WARNING:|Warn'`
	if test "$n_warn" -ne 0
	then
		summary="$summary  Warnings: $n_warn"
	fi
	n_bugs=`egrep -c 'BUG|Oops:' $1`
	if test "$n_bugs" -ne 0
	then
		summary="$summary  Bugs: $n_bugs"
	fi
	n_calltrace=`grep -c 'Call Trace:' $1`
	if test "$n_calltrace" -ne 0
	then
		summary="$summary  Call Traces: $n_calltrace"
	fi
	n_lockdep=`grep -c =========== $1`
	if test "$n_badness" -ne 0
	then
		summary="$summary  lockdep: $n_badness"
	fi
	n_stalls=`egrep -c 'detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state' $1`
	if test "$n_stalls" -ne 0
	then
		summary="$summary  Stalls: $n_stalls"
	fi
	print_warning Summary: $summary
else
	rm $1.diags
fi
