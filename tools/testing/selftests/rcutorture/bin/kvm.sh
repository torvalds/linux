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

T=/tmp/kvm.sh.$$
trap 'rm -rf $T' 0
mkdir $T

dur=30
dryrun=""
KVM="`pwd`/tools/testing/selftests/rcutorture"; export KVM
PATH=${KVM}/bin:$PATH; export PATH
builddir="${KVM}/b1"
RCU_INITRD="$KVM/initrd"; export RCU_INITRD
RCU_KMAKE_ARG=""; export RCU_KMAKE_ARG
resdir=""
configs=""
cpus=0
ds=`date +%Y.%m.%d-%H:%M:%S`
kversion=""

. functions.sh

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --bootargs kernel-boot-arguments"
	echo "       --builddir absolute-pathname"
	echo "       --buildonly"
	echo "       --configs \"config-file list\""
	echo "       --cpus N"
	echo "       --datestamp string"
	echo "       --dryrun sched|script"
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
	--cpus)
		checkarg --cpus "(number)" "$#" "$2" '^[0-9]*$' '^--'
		cpus=$2
		shift
		;;
	--datestamp)
		checkarg --datestamp "(relative pathname)" "$#" "$2" '^[^/]*$' '^--'
		ds=$2
		shift
		;;
	--dryrun)
		checkarg --dryrun "sched|script" $# "$2" 'sched\|script' '^--'
		dryrun=$2
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
if test "$dryrun" = ""
then
	echo Results directory: $resdir/$ds
	echo $scriptname $args
fi
touch $resdir/$ds/log
echo $scriptname $args >> $resdir/$ds/log

pwd > $resdir/$ds/testid.txt
if test -d .git
then
	git status >> $resdir/$ds/testid.txt
	git rev-parse HEAD >> $resdir/$ds/testid.txt
fi

touch $T/cfgcpu
for CF in $configs
do
	if test -f "$CONFIGFRAG/$kversion/$CF"
	then
		echo $CF `configNR_CPUS.sh $CONFIGFRAG/$kversion/$CF` >> $T/cfgcpu
	else
		echo "The --configs file $CF does not exist, terminating."
		exit 1
	fi
done
sort -k2nr $T/cfgcpu > $T/cfgcpu.sort

awk < $T/cfgcpu.sort > $T/cfgcpu.pack -v ncpus=$cpus '
BEGIN {
	njobs = 0;
}

{
	cf[njobs] = $1;
	cpus[njobs] = $2;
	njobs++;
}

END {
	alldone = 0;
	batch = 0;
	nc = -1;
	while (nc != ncpus) {
		batch++;
		nc = ncpus;
		for (i = 0; i < njobs; i++) {
			if (done[i])
				continue;
			if (nc >= cpus[i] || nc == ncpus) {
				done[i] = batch;
				nc -= cpus[i];
				if (nc <= 0)
					break;
			}
		}
	}
	for (b = 1; b <= batch; b++)
		for (i = 0; i < njobs; i++)
			if (done[i] == b)
				print cf[i], cpus[i];
}'

awk < $T/cfgcpu.pack \
	-v CONFIGDIR="$CONFIGFRAG/$kversion/" \
	-v KVM="$KVM" \
	-v ncpus=$cpus \
	-v rd=$resdir/$ds/ \
	-v dur=$dur \
	-v RCU_QEMU_ARG=$RCU_QEMU_ARG \
	-v RCU_BOOTARGS=$RCU_BOOTARGS \
'BEGIN {
	i = 0;
}

{
	cf[i] = $1;
	cpus[i] = $2;
	i++;
}

function dump(first, pastlast)
{
	print "echo ----start batch----"
	jn=1
	for (j = first; j < pastlast; j++) {
		builddir=KVM "/b" jn
		cpusr[jn] = cpus[j];
		if (cfrep[cf[j]] == "") {
			cfr[jn] = cf[j];
			cfrep[cf[j]] = 1;
		} else {
			cfrep[cf[j]]++;
			cfr[jn] = cf[j] "." cfrep[cf[j]];
		}
		print "echo ", cfr[jn], cpusr[jn] ": Starting build.";
		print "rm -f " builddir ".*";
		print "touch " builddir ".wait";
		print "mkdir " builddir " > /dev/null 2>&1 || :";
		print "mkdir " rd cfr[jn] " || :";
		print "kvm-test-1-rcu.sh " CONFIGDIR cf[j], builddir, rd cfr[jn], dur " \"" RCU_QEMU_ARG "\" \"" RCU_BOOTARGS "\" > " builddir ".out 2>&1 &"
		print "echo ", cfr[jn], cpusr[jn] ": Waiting for build to complete."
		print "while test -f " builddir ".wait"
		print "do"
		print "\tsleep 1"
		print "done"
		print "echo ", cfr[jn], cpusr[jn] ": Build complete."
		jn++;
	}
	for (j = 1; j < jn; j++) {
		builddir=KVM "/b" j
		print "rm -f " builddir ".ready"
		print "echo ----", cfr[j], cpusr[j] ": Starting kernel"
	}
	print "wait"
	print "echo ---- All kernel runs complete"
	for (j = 1; j < jn; j++) {
		builddir=KVM "/b" j
		print "echo ----", cfr[j], cpusr[j] ": Build/run results:"
		print "cat " builddir ".out"
	}
}

END {
	njobs = i;
	nc = ncpus;
	first = 0;
	for (i = 0; i < njobs; i++) {
		if (ncpus == 0) {
			dump(i, i + 1);
			first = i;
		} else if (nc < cpus[i] && i != 0) {
			dump(first, i);
			first = i;
			nc = ncpus;
		}
		nc -= cpus[i];
	}
	if (ncpus != 0)
		dump(first, i);
}' > $T/script

if test "$dryrun" = script
then
	echo CONFIGFRAG="$CONFIGFRAG; export CONFIGFRAG"
	echo KVM="$KVM; export KVM"
	echo KVPATH="$KVPATH; export KVPATH"
	echo PATH="$PATH; export PATH"
	echo RCU_BUILDONLY="$RCU_BUILDONLY; export RCU_BUILDONLY"
	echo RCU_INITRD="$RCU_INITRD; export RCU_INITRD"
	echo RCU_KMAKE_ARG="$RCU_KMAKE_ARG; export RCU_KMAKE_ARG"
	echo RCU_QEMU_CMD="$RCU_QEMU_CMD; export RCU_QEMU_CMD"
	echo RCU_QEMU_INTERACTIVE="$RCU_QEMU_INTERACTIVE; export RCU_QEMU_INTERACTIVE"
	echo RCU_QEMU_MAC="$RCU_QEMU_MAC; export RCU_QEMU_MAC"
	cat $T/script
	exit 0
elif test "$dryrun" = sched
then
	egrep 'start batch|Starting build\.' $T/script |
		sed -e 's/:.*$//' -e 's/^echo //'
	exit 0
else
	sh $T/script
fi

# Tracing: trace_event=rcu:rcu_grace_period,rcu:rcu_future_grace_period,rcu:rcu_grace_period_init,rcu:rcu_nocb_wake,rcu:rcu_preempt_task,rcu:rcu_unlock_preempted_task,rcu:rcu_quiescent_state_report,rcu:rcu_fqs,rcu:rcu_callback,rcu:rcu_kfree_callback,rcu:rcu_batch_start,rcu:rcu_invoke_callback,rcu:rcu_invoke_kfree_callback,rcu:rcu_batch_end,rcu:rcu_torture_read,rcu:rcu_barrier

echo " --- `date` Test summary:"
kvm-recheck.sh $resdir/$ds
