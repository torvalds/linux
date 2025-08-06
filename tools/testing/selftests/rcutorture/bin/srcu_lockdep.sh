#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Run SRCU-lockdep tests and report any that fail to meet expectations.
#
# Copyright (C) 2021 Meta Platforms, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --datestamp string"
	exit 1
}

ds=`date +%Y.%m.%d-%H.%M.%S`-srcu_lockdep
scriptname="$0"

T="`mktemp -d ${TMPDIR-/tmp}/srcu_lockdep.sh.XXXXXX`"
trap 'rm -rf $T' 0

RCUTORTURE="`pwd`/tools/testing/selftests/rcutorture"; export RCUTORTURE
PATH=${RCUTORTURE}/bin:$PATH; export PATH
. functions.sh

while test $# -gt 0
do
	case "$1" in
	--datestamp)
		checkarg --datestamp "(relative pathname)" "$#" "$2" '^[a-zA-Z0-9._/-]*$' '^--'
		ds=$2
		shift
		;;
	*)
		echo Unknown argument $1
		usage
		;;
	esac
	shift
done

nerrs=0

# Test lockdep's handling of deadlocks.
for d in 0 1
do
	for t in 0 1 2
	do
		for c in 1 2 3
		do
			err=
			val=$((d*1000+t*10+c))
			tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 5s --configs "SRCU-P" --kconfig "CONFIG_FORCE_NEED_SRCU_NMI_SAFE=y" --bootargs "rcutorture.test_srcu_lockdep=$val rcutorture.reader_flavor=0x2" --trust-make --datestamp "$ds/$val" > "$T/kvm.sh.out" 2>&1
			ret=$?
			mv "$T/kvm.sh.out" "$RCUTORTURE/res/$ds/$val"
			if ! grep -q '^CONFIG_PROVE_LOCKING=y' .config
			then
				echo "rcu_torture_init_srcu_lockdep:Error: CONFIG_PROVE_LOCKING disabled in rcutorture SRCU-P scenario"
				nerrs=$((nerrs+1))
				err=1
			fi
			if test "$d" -ne 0 && test "$ret" -eq 0
			then
				err=1
				echo -n Unexpected success for > "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
			fi
			if test "$d" -eq 0 && test "$ret" -ne 0
			then
				err=1
				echo -n Unexpected failure for > "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
			fi
			if test -n "$err"
			then
				grep "rcu_torture_init_srcu_lockdep: test_srcu_lockdep = " "$RCUTORTURE/res/$ds/$val/SRCU-P/console.log" | sed -e 's/^.*rcu_torture_init_srcu_lockdep://' >> "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
				cat "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
				nerrs=$((nerrs+1))
			fi
		done
	done
done

# Test lockdep-enabled testing of mixed SRCU readers.
for val in 0x1 0xf
do
	err=
	tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 5s --configs "SRCU-P" --kconfig "CONFIG_FORCE_NEED_SRCU_NMI_SAFE=y" --bootargs "rcutorture.reader_flavor=$val" --trust-make --datestamp "$ds/$val" > "$T/kvm.sh.out" 2>&1
	ret=$?
	mv "$T/kvm.sh.out" "$RCUTORTURE/res/$ds/$val"
	if ! grep -q '^CONFIG_PROVE_LOCKING=y' .config
	then
		echo "rcu_torture_init_srcu_lockdep:Error: CONFIG_PROVE_LOCKING disabled in rcutorture SRCU-P scenario"
		nerrs=$((nerrs+1))
		err=1
	fi
	if test "$val" -eq 0xf && test "$ret" -eq 0
	then
		err=1
		echo -n Unexpected success for > "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
	fi
	if test "$val" -eq 0x1 && test "$ret" -ne 0
	then
		err=1
		echo -n Unexpected failure for > "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
	fi
	if test -n "$err"
	then
		grep "rcu_torture_init_srcu_lockdep: test_srcu_lockdep = " "$RCUTORTURE/res/$ds/$val/SRCU-P/console.log" | sed -e 's/^.*rcu_torture_init_srcu_lockdep://' >> "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
		cat "$RCUTORTURE/res/$ds/$val/kvm.sh.err"
		nerrs=$((nerrs+1))
	fi
done

# Set up exit code.
if test "$nerrs" -ne 0
then
	exit 1
fi
exit 0
