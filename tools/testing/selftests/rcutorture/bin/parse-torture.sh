#!/bin/bash
#
# Check the console output from a torture run for goodness.
# The "file" is a pathname on the local system, and "title" is
# a text string for error-message purposes.
#
# The file must contain torture output, but can be interspersed
# with other dmesg text, as in console-log output.
#
# Usage: parse-torture.sh file title
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

T=${TMPDIR-/tmp}/parse-torture.sh.$$
file="$1"
title="$2"

trap 'rm -f $T.seq' 0

. functions.sh

# check for presence of torture output file.

if test -f "$file" -a -r "$file"
then
	:
else
	echo $title unreadable torture output file: $file
	exit 1
fi

# check for abject failure

if grep -q FAILURE $file || grep -q -e '-torture.*!!!' $file
then
	nerrs=`grep --binary-files=text '!!!' $file | tail -1 | awk '{for (i=NF-8;i<=NF;i++) sum+=$i; } END {print sum}'`
	print_bug $title FAILURE, $nerrs instances
	echo "   " $url
	exit
fi

grep --binary-files=text 'torture:.*ver:' $file | grep --binary-files=text -v '(null)' | sed -e 's/^(initramfs)[^]]*] //' -e 's/^\[[^]]*] //' |
awk '
BEGIN	{
	ver = 0;
	badseq = 0;
	}

	{
	if (!badseq && ($5 + 0 != $5 || $5 <= ver)) {
		badseqno1 = ver;
		badseqno2 = $5;
		badseqnr = NR;
		badseq = 1;
	}
	ver = $5
	}

END	{
	if (badseq) {
		if (badseqno1 == badseqno2 && badseqno2 == ver)
			print "GP HANG at " ver " torture stat " badseqnr;
		else
			print "BAD SEQ " badseqno1 ":" badseqno2 " last:" ver " version " badseqnr;
	}
	}' > $T.seq

if grep -q SUCCESS $file
then
	if test -s $T.seq
	then
		print_warning $title $title `cat $T.seq`
		echo "   " $file
		exit 2
	fi
else
	if grep -q "_HOTPLUG:" $file
	then
		print_warning HOTPLUG FAILURES $title `cat $T.seq`
		echo "   " $file
		exit 3
	fi
	echo $title no success message, `grep --binary-files=text 'ver:' $file | wc -l` successful version messages
	if test -s $T.seq
	then
		print_warning $title `cat $T.seq`
	fi
	exit 2
fi
