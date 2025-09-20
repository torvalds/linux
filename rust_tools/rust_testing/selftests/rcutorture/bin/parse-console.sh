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

T="`mktemp -d ${TMPDIR-/tmp}/parse-console.sh.XXXXXX`"
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

# Check for proper termination, except for rcuscale and refscale.
if test "$TORTURE_SUITE" != rcuscale && test "$TORTURE_SUITE" != refscale
then
	# check for abject failure

	if grep -q FAILURE $file || grep -q -e '-torture.*!!!' $file
	then
		nerrs=`grep --binary-files=text '!!!' $file |
		tail -1 |
		awk '
		{
			normalexit = 1;
			for (i=NF-8;i<=NF;i++) {
				if (i <= 0 || i !~ /^[0-9]*$/) {
					bangstring = $0;
					gsub(/^\[[^]]*] /, "", bangstring);
					print bangstring;
					normalexit = 0;
					exit 0;
				}
				sum+=$i;
			}
		}
		END {
			if (normalexit)
				print sum " instances"
		}'`
		print_bug $title FAILURE, $nerrs
		exit
	fi

	grep --binary-files=text 'torture:.*ver:' $file |
	grep -E --binary-files=text -v '\(null\)|rtc: 000000000* ' |
	sed -e 's/^(initramfs)[^]]*] //' -e 's/^\[[^]]*] //' |
	sed -e 's/^.*ver: //' |
	awk '
	BEGIN	{
		ver = 0;
		badseq = 0;
		}

		{
		if (!badseq && ($1 + 0 != $1 || $1 <= ver)) {
			badseqno1 = ver;
			badseqno2 = $1;
			badseqnr = NR;
			badseq = 1;
		}
		ver = $1
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

console-badness.sh < $file > $T.diags
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
	n_warn=`grep -v 'Warning: unable to open an initial console' $file | grep -v 'Warning: Failed to add ttynull console. No stdin, stdout, and stderr for the init process' | grep -E -c 'WARNING:|Warn'`
	if test "$n_warn" -ne 0
	then
		summary="$summary  Warnings: $n_warn"
	fi
	n_bugs=`grep -E -c '\bBUG|Oops:' $file`
	if test "$n_bugs" -ne 0
	then
		summary="$summary  Bugs: $n_bugs"
	fi
	n_kcsan=`grep -E -c 'BUG: KCSAN: ' $file`
	if test "$n_kcsan" -ne 0
	then
		if test "$n_bugs" = "$n_kcsan"
		then
			summary="$summary (all bugs kcsan)"
		else
			summary="$summary  KCSAN: $n_kcsan"
		fi
	fi
	n_calltrace=`grep -Ec 'Call Trace:|Call trace:' $file`
	if test "$n_calltrace" -ne 0
	then
		summary="$summary  Call Traces: $n_calltrace"
	fi
	n_lockdep=`grep -c =========== $file`
	if test "$n_badness" -ne 0
	then
		summary="$summary  lockdep: $n_badness"
	fi
	n_stalls=`grep -E -c 'detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state' $file`
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

# Call extract_ftrace_from_console function, if the output is empty,
# don't create $file.ftrace. Otherwise output the results to $file.ftrace
extract_ftrace_from_console $file > $file.ftrace
if [ ! -s $file.ftrace ]; then
	rm -f $file.ftrace
fi
