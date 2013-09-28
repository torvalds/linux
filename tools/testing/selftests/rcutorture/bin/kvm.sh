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

dur=30
KVM=`pwd`/tools/testing/selftests/rcutorture; export KVM
builddir=${KVM}/b1
resdir=""
configs=" sysidleY.2013.06.19a \
	  sysidleN.2013.06.19a \
	  P1-S-T-NH-SD-SMP-HP \
	  P2-2-t-nh-sd-SMP-hp \
	  P3-3-T-nh-SD-SMP-hp \
	  P4-A-t-NH-sd-SMP-HP \
	  P5-U-T-NH-sd-SMP-hp \
	  P6---t-nh-SD-smp-hp \
	  N1-S-T-NH-SD-SMP-HP \
	  N2-2-t-nh-sd-SMP-hp \
	  N3-3-T-nh-SD-SMP-hp \
	  N4-A-t-NH-sd-SMP-HP \
	  N5-U-T-NH-sd-SMP-hp \
	  PT1-nh \
	  PT2-NH \
	  NT1-nh \
	  NT3-NH"

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --builddir absolute-pathname"
	echo "       --configs \"config-file list\""
	echo "       --duration minutes"
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
	echo ":$1:"
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
	--duration)
		checkarg --duration "(minutes)" $# "$2" '^[0-9]*$' error
		dur=$2
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

echo "builddir=$builddir"
echo "dur=$dur"
echo "KVM=$KVM"
echo "resdir=$resdir"

PATH=${KVM}/bin:$PATH; export PATH
CONFIGFRAG=${KVM}/configs; export CONFIGFRAG

if test -z "$resdir"
then
	resdir=$KVM/res
	mkdir $resdir || :
	ds=`date +%Y.%m.%d-%H:%M:%S`
	mkdir $resdir/$ds
	echo Datestamp: $ds
else
	mkdir -p "$resdir"
	ds=""
fi
pwd > $resdir/$ds/testid.txt
if test -d .git
then
	git status >> $resdir/$ds/testid.txt
	git rev-parse HEAD >> $resdir/$ds/testid.txt
fi
builddir=$KVM/b1
mkdir $builddir || :

for CF in $configs
do
	rd=$resdir/$ds/$CF
	mkdir $rd || :
	echo Results directory: $rd
	kvm-test-1-rcu.sh $CONFIGFRAG/$CF $builddir $rd $dur "-nographic" "rcutorture.test_no_idle_hz=1 rcutorture.n_barrier_cbs=4 rcutorture.verbose=1"
done
# Tracing: trace_event=rcu:rcu_nocb_grace_period,rcu:rcu_grace_period,rcu:rcu_grace_period_init,rcu:rcu_quiescent_state_report,rcu:rcu_fqs,rcu:rcu_callback,rcu:rcu_torture_read,rcu:rcu_invoke_callback,rcu:rcu_fqs,rcu:rcu_dyntick,rcu:rcu_unlock_preempted_task
