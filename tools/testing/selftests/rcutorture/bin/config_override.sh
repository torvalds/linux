#!/bin/bash
#
# config_override.sh base override
#
# Combines base and override, removing any Kconfig options from base
# that conflict with any in override, concatenating what remains and
# sending the result to standard output.
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
# Copyright (C) IBM Corporation, 2017
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

base=$1
if test -r $base
then
	:
else
	echo Base file $base unreadable!!!
	exit 1
fi

override=$2
if test -r $override
then
	:
else
	echo Override file $override unreadable!!!
	exit 1
fi

T=/tmp/config_override.sh.$$
trap 'rm -rf $T' 0
mkdir $T

sed < $override -e 's/^/grep -v "/' -e 's/=.*$/="/' |
	awk '
	{
		if (last)
			print last " |";
		last = $0;
	}
	END {
		if (last)
			print last;
	}' > $T/script
sh $T/script < $base
cat $override
