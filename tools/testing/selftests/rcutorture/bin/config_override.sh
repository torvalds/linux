#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# config_override.sh base override
#
# Combines base and override, removing any Kconfig options from base
# that conflict with any in override, concatenating what remains and
# sending the result to standard output.
#
# Copyright (C) IBM Corporation, 2017
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

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

T=${TMPDIR-/tmp}/config_override.sh.$$
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
