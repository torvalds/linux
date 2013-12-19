#!/bin/bash
#
# Run a series of 14 tests under KVM.  These are not particularly
# well-selected or well-tuned, but are the current set.  Run from the
# top level of the source tree.
#
# Edit the definitions below to set the locations of the various directories,
# as well as the test duration.
#
# Usage: sh kvm.sh [ options ]
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

scriptname=$0
args="$*"

dur=30
KVM="`pwd`/tools/testing/selftests/rcutorture"; export KVM
PATH=${KVM}/bin:$PATH; export PATH
builddir="${KVM}/b1"
RCU_INITRD="$KVM/initrd"; export RCU_INITRD
RCU_KMAKE_ARG=""; export RCU_KMAKE_ARG
resdir=""
configs=""
ds=`date +%Y.%m.%d-%H:%M:%S`
kversion=""

. functions.sh

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --bootargs kernel-boot-arguments"
	echo "       --builddir absolute-pathname"
	echo "       --buildonly"
	echo "       --configs \"config-file list\""
	echo "       --datestamp string"
	echo "       --duration minutes"
	echo "       --interactive"
	echo "       --kmake-arg kernel-make-arguments"
	echo "       --kversion vN.NN"
	echo "       --mac nn:nn:nn:nn:nn:nn"
	echo "       --no-initrd"
	echo "       --qemu-args qemu-system-..."
	echo "       --qemu-cmd qemu-system-..."
	echo "       --results absolute-pathname"
	echo "       --relbuilddir relative-pathname"
	exit 1
}

while test $# -gt 0
do
	case "$1" in
	--bootargs)
		checkarg --bootargs "(list of kernel boot arguments)" "$#" "$2" '.*' '^--'
		RCU_BOOTARGS="$2"
		shift
		;;
	--builddir)
		checkarg --builddir "(absolute pathname)" "$#" "$2" '^/' '^error'
		builddir=$2
		gotbuilddir=1
		shift
		;;
	--buildonly)
		RCU_BUILDONLY=1; export RCU_BUILDONLY
		;;
	--configs)
		checkarg --configs "(list of config files)" "$#" "$2" '^[^/]*$' '^--'
		configs="$2"
		shift
		;;
	--datestamp)
		checkarg --datestamp "(relative pathname)" "$#" "$2" '^[^/]*$' '^--'
		ds=$2
		shift
		;;
	--duration)
		checkarg --duration "(minutes)" $# "$2" '^[0-9]*$' '^error'
		dur=$2
		shift
		;;
	--interactive)
		RCU_QEMU_INTERACTIVE=1; export RCU_QEMU_INTERACTIVE
		;;
	--kmake-arg)
		checkarg --kmake-arg "(kernel make arguments)" $# "$2" '.*' '^error$'
		RCU_KMAKE_ARG="$2"; export RCU_KMAKE_ARG
		shift
		;;
	--kversion)
		checkarg --kversion "(kernel version)" $# "$2" '^v[0-9.]*$' '^error'
		kversion=$2
		shift
		;;
	--mac)
		checkarg --mac "(MAC address)" $# "$2" '^\([0-9a-fA-F]\{2\}:\)\{5\}[0-9a-fA-F]\{2\}$' error
		RCU_QEMU_MAC=$2; export RCU_QEMU_MAC
		shift
		;;
	--no-initrd)
		RCU_INITRD=""; export RCU_INITRD
		;;
	--qemu-args)
		checkarg --qemu-args "-qemu args" $# "$2" '^-' '^error'
		RCU_QEMU_ARG="$2"
		shift
		;;
	--qemu-cmd)
		checkarg --qemu-cmd "(qemu-system-...)" $# "$2" 'qemu-system-' '^--'
		RCU_QEMU_CMD="$2"; export RCU_QEMU_CMD
		shift
		;;
	--relbuilddir)
		checkarg --relbuilddir "(relative pathname)" "$#" "$2" '^[^/]*$' '^--'
		relbuilddir=$2
		gotrelbuilddir=1
		builddir=${KVM}/${relbuilddir}
		shift
		;;
	--results)
		checkarg --results "(absolute pathname)" "$#" "$2" '^/' '^error'
		resdir=$2
		shift
		;;
	*)
		echo Unknown argument $1
		usage
		;;
	esac
	shift
done

CONFIGFRAG=${KVM}/configs; export CONFIGFRAG
KVPATH=${CONFIGFRAG}/$kversion; export KVPATH

if test -z "$configs"
then
	configs="`cat $CONFIGFRAG/$kversion/CFLIST`"
fi

if test -z "$resdir"
then
	resdir=$KVM/res
	if ! test -e $resdir
	then
		mkdir $resdir || :
	fi
else
	if ! test -e $resdir
	then
		mkdir -p "$resdir" || :
	fi
fi
mkdir $resdir/$ds
touch $resdir/$ds/log
echo $scriptname $args
echo $scriptname $args >> $resdir/$ds/log

pwd > $resdir/$ds/testid.txt
if test -d .git
then
	git status >> $resdir/$ds/testid.txt
	git rev-parse HEAD >> $resdir/$ds/testid.txt
fi
builddir=$KVM/b1
if ! test -e $builddir
then
	mkdir $builddir || :
fi

for CF in $configs
do
	# Running TREE01 multiple times creates TREE01, TREE01.2, TREE01.3, ...
	rd=$resdir/$ds/$CF
	if test -d "${rd}"
	then
		n="`ls -d "${rd}"* | grep '\.[0-9]\+$' |
			sed -e 's/^.*\.\([0-9]\+\)/\1/' |
			sort -k1n | tail -1`"
		if test -z "$n"
		then
			rd="${rd}.2"
		else
			n="`expr $n + 1`"
			rd="${rd}.${n}"
		fi
	fi
	mkdir "${rd}"
	echo Results directory: $rd
	kvm-test-1-rcu.sh $CONFIGFRAG/$kversion/$CF $builddir $rd $dur "-nographic $RCU_QEMU_ARG" "rcutorture.test_no_idle_hz=1 rcutorture.verbose=1 $RCU_BOOTARGS"
done
# Tracing: trace_event=rcu:rcu_grace_period,rcu:rcu_future_grace_period,rcu:rcu_grace_period_init,rcu:rcu_nocb_wake,rcu:rcu_preempt_task,rcu:rcu_unlock_preempted_task,rcu:rcu_quiescent_state_report,rcu:rcu_fqs,rcu:rcu_callback,rcu:rcu_kfree_callback,rcu:rcu_batch_start,rcu:rcu_invoke_callback,rcu:rcu_invoke_kfree_callback,rcu:rcu_batch_end,rcu:rcu_torture_read,rcu:rcu_barrier

echo " --- `date` Test summary:"
kvm-recheck.sh $resdir/$ds
