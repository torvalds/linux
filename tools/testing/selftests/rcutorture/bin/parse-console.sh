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

T=/tmp/abat-chk-badness.sh.$$
trap 'rm -f $T' 0

file="$1"
title="$2"

. functions.sh

if grep -Pq '\x00' < $file
then
	print_warning Console output contains nul bytes, old qemu still running?
fi
egrep 'Badness|WARNING:|Warn|BUG|===========|Call Trace:|Oops:|Stall ended before state dump start' < $file | grep -v 'ODEBUG: ' | grep -v 'Warning: unable to open an initial console' > $T
if test -s $T
then
	print_warning Assertion failure in $file $title
	cat $T
fi
