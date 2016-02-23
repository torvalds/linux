#!/bin/bash
#
# Check the build output from an rcutorture run for goodness.
# The "file" is a pathname on the local system, and "title" is
# a text string for error-message purposes.
#
# The file must contain kernel build output.
#
# Usage: parse-build.sh file title
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

F=$1
title=$2
T=/tmp/parse-build.sh.$$
trap 'rm -rf $T' 0
mkdir $T

. functions.sh

if grep -q CC < $F
then
	:
else
	print_bug $title no build
	exit 1
fi

if grep -q "error:" < $F
then
	print_bug $title build errors:
	grep "error:" < $F
	exit 2
fi

grep warning: < $F > $T/warnings
grep "include/linux/*rcu*\.h:" $T/warnings > $T/hwarnings
grep "kernel/rcu/[^/]*:" $T/warnings > $T/cwarnings
cat $T/hwarnings $T/cwarnings > $T/rcuwarnings
if test -s $T/rcuwarnings
then
	print_warning $title build errors:
	cat $T/rcuwarnings
	exit 2
fi
exit 0
