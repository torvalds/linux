#!/bin/bash
#
# Analyze a given results directory for rcutorture progress.
#
# Usage: sh kvm-recheck-rcu.sh resdir
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
# Copyright (C) IBM Corporation, 2014
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

i="$1"
if test -d $i
then
	:
else
	echo Unreadable results directory: $i
	exit 1
fi

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
		ngpsps=`awk -v ngps=$ngps -v dur=$dur '
			BEGIN { print ngps / dur }' < /dev/null`
		title="$title ($ngpsps per second)"
	fi
	echo $title
fi
