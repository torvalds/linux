#!/bin/bash
#
# Given the results directories for previous KVM runs of rcutorture,
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
	dirs=`find $rd -name Make.defconfig.out -print | sort | sed -e 's,/[^/]*$,,' | sort -u`
	for i in $dirs
	do
		configfile=`echo $i | sed -e 's/^.*\///'`
		ngps=`grep ver: $i/console.log 2> /dev/null | tail -1 | sed -e 's/^.* ver: //' -e 's/ .*$//'`
		if test -z "$ngps"
		then
			echo $configfile
		else
			title="$configfile ------- $ngps grace periods"
			dur=`sed -e 's/^.* rcutorture.shutdown_secs=//' -e 's/ .*$//' < $i/qemu-cmd 2> /dev/null`
			if test -z "$dur"
			then
				:
			else
				ngpsps=$((ngps / dur))
				ngpsps=`awk -v ngps=$ngps -v dur=$dur '
					BEGIN { print ngps / dur }' < /dev/null`
				title="$title ($ngpsps per second)"
			fi
			echo $title
		fi
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
