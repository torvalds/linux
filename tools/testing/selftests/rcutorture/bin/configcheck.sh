#!/bin/sh
# Usage: sh configcheck.sh .config .config-template
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

T=/tmp/abat-chk-config.sh.$$
trap 'rm -rf $T' 0
mkdir $T

cat $1 > $T/.config

cat $2 | sed -e 's/\(.*\)=n/# \1 is not set/' -e 's/^#CHECK#//' |
awk	'
BEGIN	{
		print "if grep -q \"" $0 "\" < '"$T/.config"'";
		print "then";
		print "\t:";
		print "else";
		if ($1 == "#") {
			print "\tif grep -q \"" $2 "\" < '"$T/.config"'";
			print "\tthen";
			print "\t\tif test \"$firsttime\" = \"\""
			print "\t\tthen"
			print "\t\t\tfirsttime=1"
			print "\t\tfi"
			print "\t\techo \":" $2 ": improperly set\"";
			print "\telse";
			print "\t\t:";
			print "\tfi";
		} else {
			print "\tif test \"$firsttime\" = \"\""
			print "\tthen"
			print "\t\tfirsttime=1"
			print "\tfi"
			print "\techo \":" $0 ": improperly set\"";
		}
		print "fi";
	}' | sh
