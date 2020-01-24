#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Check the console output from an rcutorture run for oopses.
# The "file" is a pathname on the local system, and "title" is
# a text string for error-message purposes.
#
# Usage: parse-console.sh file title
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

T=${TMPDIR-/tmp}/parse-console.sh.$$
file="$1"
title="$2"

trap 'rm -f $T.seq $T.diags' 0

. functions.sh

# Check for presence and readability of console output file
if test -f "$file" -a -r "$file"
then
	:
else
	echo $title unreadable console output file: $file
	exit 1
fi
if grep -Pq '\x00' < $file
then
	print_warning Console output contains nul bytes, old qemu still running?
fi
cat /dev/null > $file.diags

# Check for proper termination, except that rcuperf runs don't indicate this.
if test "$TORTURE_SUITE" != rcuperf
then
	# check for abject failure

	if grep -q FAILURE $file || grep -q -e '-torture.*!!!' $file
	then
		nerrs=`grep --binary-files=text '!!!' $file |
		tail -1 |
		awk '
		{
			for (i=NF-8;i<=NF;i++)
				sum+=$i;
		}
		END { print sum }'`
		print_bug $title FAILURE, $nerrs instances
		exit
	fi

	grep --binary-files=text 'torture:.*ver:' $file |
	egrep --binary-files=text -v '\(null\)|rtc: 000000000* ' |
	sed -e 's/^(initramfs)[^]]*] //' -e 's/^\[[^]]*] //' |
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
			print_warning $title `cat $T.seq`
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
fi | tee -a $file.diags

egrep 'Badness|WARNING:|Warn|BUG|===========|Call Trace:|Oops:|detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state|rcu_.*kthread starved for' < $file |
grep -v 'ODEBUG: ' |
grep -v 'This means that this is a DEBUG kernel and it is' |
grep -v 'Warning: unable to open an initial console' > $T.diags
if test -s $T.diags
then
	print_warning "Assertion failure in $file $title"
	# cat $T.diags
	summary=""
	n_badness=`grep -c Badness $file`
	if test "$n_badness" -ne 0
	then
		summary="$summary  Badness: $n_badness"
	fi
	n_warn=`grep -v 'Warning: unable to open an initial console' $file | egrep -c 'WARNING:|Warn'`
	if test "$n_warn" -ne 0
	then
		summary="$summary  Warnings: $n_warn"
	fi
	n_bugs=`egrep -c 'BUG|Oops:' $file`
	if test "$n_bugs" -ne 0
	then
		summary="$summary  Bugs: $n_bugs"
	fi
	n_calltrace=`grep -c 'Call Trace:' $file`
	if test "$n_calltrace" -ne 0
	then
		summary="$summary  Call Traces: $n_calltrace"
	fi
	n_lockdep=`grep -c =========== $file`
	if test "$n_badness" -ne 0
	then
		summary="$summary  lockdep: $n_badness"
	fi
	n_stalls=`egrep -c 'detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state' $file`
	if test "$n_stalls" -ne 0
	then
		summary="$summary  Stalls: $n_stalls"
	fi
	n_starves=`grep -c 'rcu_.*kthread starved for' $file`
	if test "$n_starves" -ne 0
	then
		summary="$summary  Starves: $n_starves"
	fi
	print_warning Summary: $summary
	cat $T.diags >> $file.diags
fi
for i in $file.*.diags
do
	if test -f "$i"
	then
		cat $i >> $file.diags
	fi
done
if ! test -s $file.diags
then
	rm -f $file.diags
fi
