#!/bin/bash
#
# Given the results directories for previous KVM-based torture runs,
# check the build and console output for errors.  Given a directory
# containing results directories, this recursively checks them all.
#
# Usage: sh kvm-recheck.sh resdir ...
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

PATH=`pwd`/tools/testing/selftests/rcutorture/bin:$PATH; export PATH
for rd in "$@"
do
	firsttime=1
	dirs=`find $rd -name Make.defconfig.out -print | sort | sed -e 's,/[^/]*$,,' | sort -u`
	for i in $dirs
	do
		if test -n "$firsttime"
		then
			firsttime=""
			resdir=`echo $i | sed -e 's,/$,,' -e 's,/[^/]*$,,'`
			head -1 $resdir/log
		fi
		TORTURE_SUITE="`cat $i/../TORTURE_SUITE`"
		kvm-recheck-${TORTURE_SUITE}.sh $i
		configcheck.sh $i/.config $i/ConfigFragment
		parse-build.sh $i/Make.out $configfile
		parse-rcutorture.sh $i/console.log $configfile
		parse-console.sh $i/console.log $configfile
		if test -r $i/Warnings
		then
			cat $i/Warnings
		fi
	done
done
