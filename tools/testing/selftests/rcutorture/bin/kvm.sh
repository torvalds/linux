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
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

scriptname=$0
args="$*"

dur=30
KVM=`pwd`/tools/testing/selftests/rcutorture; export KVM
builddir=${KVM}/b1
resdir=""
configs=""
ds=`date +%Y.%m.%d-%H:%M:%S`
kversion=""

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --builddir absolute-pathname"
	echo "       --configs \"config-file list\""
	echo "       --datestamp string"
	echo "       --duration minutes"
	echo "       --kversion vN.NN"
	echo "       --qemu-cmd qemu-system-..."
	echo "       --rcu-kvm absolute-pathname"
	echo "       --results absolute-pathname"
	echo "       --relbuilddir relative-pathname"
	exit 1
}

# checkarg --argname argtype $# arg mustmatch cannotmatch
checkarg () {
	if test $3 -le 1
	then
		echo $1 needs argument $2 matching \"$5\"
		usage
	fi
	if echo "$4" | grep -q -e "$5"
	then
		:
	else
		echo $1 $2 \"$4\" must match \"$5\"
		usage
	fi
	if echo "$4" | grep -q -e "$6"
	then
		echo $1 $2 \"$4\" must not match \"$6\"
		usage
	fi
}

while test $# -gt 0
do
	case "$1" in
	--builddir)
		checkarg --builddir "(absolute pathname)" "$#" "$2" '^/' error
		builddir=$2
		gotbuilddir=1
		shift
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
		checkarg --duration "(minutes)" $# "$2" '^[0-9]*$' error
		dur=$2
		shift
		;;
	--kversion)
		checkarg --kversion "(kernel version)" $# "$2" '^v[0-9.]*$' error
		kversion=$2
		shift
		;;
	--qemu-cmd)
		checkarg --qemu-cmd "(qemu-system-...)" $# "$2" 'qemu-system-' '^--'
		RCU_QEMU_CMD="$2"; export RCU_QEMU_CMD
		shift
		;;
	--rcu-kvm)
		checkarg --rcu-kvm "(absolute pathname)" "$#" "$2" '^/' error
		KVM=$2; export KVM
		if -z "$gotbuilddir"
		then
			builddir=${KVM}/b1
		fi
		if -n "$gotrelbuilddir"
		then
			builddir=${KVM}/${relbuilddir}
		fi
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
		checkarg --results "(absolute pathname)" "$#" "$2" '^/' error
		resdir=$2
		shift
		;;
	*)
		usage
		;;
	esac
	shift
done

PATH=${KVM}/bin:$PATH; export PATH
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
	rd=$resdir/$ds/$CF
	mkdir $rd || :
	echo Results directory: $rd
	kvm-test-1-rcu.sh $CONFIGFRAG/$kversion/$CF $builddir $rd $dur "-nographic" "rcutorture.test_no_idle_hz=1 rcutorture.verbose=1"
done
# Tracing: trace_event=rcu:rcu_nocb_grace_period,rcu:rcu_grace_period,rcu:rcu_grace_period_init,rcu:rcu_quiescent_state_report,rcu:rcu_fqs,rcu:rcu_callback,rcu:rcu_torture_read,rcu:rcu_invoke_callback,rcu:rcu_fqs,rcu:rcu_dyntick,rcu:rcu_unlock_preempted_task
