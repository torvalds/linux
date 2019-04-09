#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Given the results directories for previous KVM-based torture runs,
# check the build and console output for errors.  Given a directory
# containing results directories, this recursively checks them all.
#
# Usage: kvm-recheck.sh resdir ...
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

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
		TORTURE_SUITE="`cat $i/../TORTURE_SUITE`"
		rm -f $i/console.log.*.diags
		kvm-recheck-${TORTURE_SUITE}.sh $i
		if test -f "$i/console.log"
		then
			configcheck.sh $i/.config $i/ConfigFragment
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
			if test -f "$i/qemu-cmd"
			then
				print_bug qemu failed
				echo "   $i"
			elif test -f "$i/buildonly"
			then
				echo Build-only run, no boot/test
				configcheck.sh $i/.config $i/ConfigFragment
				parse-build.sh $i/Make.out $configfile
			else
				print_bug Build failed
				echo "   $i"
			fi
		fi
	done
done
