#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Given the results directories for previous KVM-based torture runs,
# check the build and console output for errors.  Given a directory
# containing results directories, this recursively checks them all.
#
# Usage: kvm-recheck.sh resdir ...
#
# Returns status reflecting the success or not of the last run specified.
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

T=/tmp/kvm-recheck.sh.$$
trap 'rm -f $T' 0 2

configerrors=0

PATH=`pwd`/tools/testing/selftests/rcutorture/bin:$PATH; export PATH
. functions.sh
for rd in "$@"
do
	firsttime=1
	dirs=`find $rd -name Make.defconfig.out -print | sort | sed -e 's,/[^/]*$,,' | sort -u`
	for i in $dirs
	do
		if test -n "$firsttime"
		then
			firsttime=""
			resdir=`echo $i | sed -e 's,/$,,' -e 's,/[^/]*$,,'`
			head -1 $resdir/log
		fi
		TORTURE_SUITE="`cat $i/../torture_suite`" ; export TORTURE_SUITE
		configfile=`echo $i | sed -e 's,^.*/,,'`
		rm -f $i/console.log.*.diags $i/ConfigFragment.diags
		case "${TORTURE_SUITE}" in
		X*)
			;;
		*)
			kvm-recheck-${TORTURE_SUITE}.sh $i
		esac
		if test -f "$i/qemu-retval" && test "`cat $i/qemu-retval`" -ne 0 && test "`cat $i/qemu-retval`" -ne 137
		then
			echo QEMU error, output:
			cat $i/qemu-output
		elif test -f "$i/console.log"
		then
			if test -f "$i/qemu-retval" && test "`cat $i/qemu-retval`" -eq 137
			then
				echo QEMU killed
			fi
			configcheck.sh $i/.config $i/ConfigFragment > $i/ConfigFragment.diags 2>&1
			if grep -q '^CONFIG_KCSAN=y$' $i/ConfigFragment.input
			then
				# KCSAN forces a number of Kconfig options, so remove
				# complaints about those Kconfig options in KCSAN runs.
				mv $i/ConfigFragment.diags $i/ConfigFragment.diags.kcsan
				grep -v -E 'CONFIG_PROVE_RCU|CONFIG_PREEMPT_COUNT' $i/ConfigFragment.diags.kcsan > $i/ConfigFragment.diags
			fi
			if test -s $i/ConfigFragment.diags
			then
				cat $i/ConfigFragment.diags
				configerrors=$((configerrors+1))
			else
				rm $i/ConfigFragment.diags
			fi
			if test -r $i/Make.oldconfig.err
			then
				cat $i/Make.oldconfig.err
			fi
			parse-build.sh $i/Make.out $configfile
			parse-console.sh $i/console.log $configfile
			if test -r $i/Warnings
			then
				cat $i/Warnings
			fi
		else
			if test -f "$i/buildonly"
			then
				echo Build-only run, no boot/test
				configcheck.sh $i/.config $i/ConfigFragment > $i/ConfigFragment.diags 2>&1
				if test -s $i/ConfigFragment.diags
				then
					cat $i/ConfigFragment.diags
					configerrors=$((configerrors+1))
				else
					rm $i/ConfigFragment.diags
				fi
				parse-build.sh $i/Make.out $configfile
			elif test -f "$i/qemu-cmd"
			then
				print_bug qemu failed
				echo "   $i"
			else
				print_bug Build failed
				echo "   $i"
			fi
		fi
	done
	if test -f "$rd/kcsan.sum"
	then
		if ! test -f $i/ConfigFragment.diags
		then
			:
		elif grep -q CONFIG_KCSAN=y $i/ConfigFragment.diags
		then
			echo "Compiler or architecture does not support KCSAN!"
			echo Did you forget to switch your compiler with '--kmake-arg CC=<cc-that-supports-kcsan>'?
		elif test -s "$rd/kcsan.sum"
		then
			echo KCSAN summary in $rd/kcsan.sum
		else
			echo Clean KCSAN run in $rd
		fi
	fi
done

if test "$configerrors" -gt 0
then
	echo $configerrors runs with .config errors.
	ret=1
fi
EDITOR=echo kvm-find-errors.sh "${@: -1}" > $T 2>&1
builderrors="`tr ' ' '\012' < $T | grep -c '/Make.out.diags'`"
if test "$builderrors" -gt 0
then
	echo $builderrors runs with build errors.
	ret=2
fi
runerrors="`tr ' ' '\012' < $T | grep -c '/console.log.diags'`"
if test "$runerrors" -gt 0
then
	echo $runerrors runs with runtime errors.
	ret=3
fi
exit $ret
