#!/bin/bash
#
# Analyze a given results directory for locktorture progress.
#
# Usage: sh kvm-recheck-lock.sh resdir
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
ncs=`grep "Writes:  Total:" $i/console.log 2> /dev/null | tail -1 | sed -e 's/^.* Total: //' -e 's/ .*$//'`
if test -z "$ncs"
then
	echo $configfile
else
	title="$configfile ------- $ncs acquisitions/releases"
	dur=`sed -e 's/^.* locktorture.shutdown_secs=//' -e 's/ .*$//' < $i/qemu-cmd 2> /dev/null`
	if test -z "$dur"
	then
		:
	else
		ncsps=`awk -v ncs=$ncs -v dur=$dur '
			BEGIN { print ncs / dur }' < /dev/null`
		title="$title ($ncsps per second)"
	fi
	echo $title
fi
