#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Usage: configcheck.sh .config .config-template
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

T="`mktemp -d ${TMPDIR-/tmp}/configcheck.sh.XXXXXX`"
trap 'rm -rf $T' 0

cat $1 > $T/.config

cat $2 | sed -e 's/\(.*\)=n/# \1 is not set/' -e 's/^#CHECK#//' |
grep -v '^CONFIG_INITRAMFS_SOURCE' |
awk	'
{
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
