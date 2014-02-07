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
fi

if test "$dryrun" = ""
then
	if ! test -e $resdir
	then
		mkdir -p "$resdir" || :
	fi
	mkdir $resdir/$ds

	# Be noisy only if running the script.
	echo Results directory: $resdir/$ds
	echo $scriptname $args

	touch $resdir/$ds/log
	echo $scriptname $args >> $resdir/$ds/log

	pwd > $resdir/$ds/testid.txt
	if test -d .git
	then
		git status >> $resdir/$ds/testid.txt
		git rev-parse HEAD >> $resdir/$ds/testid.txt
	fi
fi

# Create a file of test-name/#cpus pairs, sorted by decreasing #cpus.
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

# Use a greedy bin-packing algorithm, sorting the list accordingly.
awk < $T/cfgcpu.sort > $T/cfgcpu.pack -v ncpus=$cpus '
BEGIN {
	njobs = 0;
}

{
	# Read file of tests and corresponding required numbers of CPUs.
	cf[njobs] = $1;
	cpus[njobs] = $2;
	njobs++;
}

END {
	alldone = 0;
	batch = 0;
	nc = -1;

	# Each pass through the following loop creates on test batch
	# that can be executed concurrently given ncpus.  Note that a
	# given test that requires more than the available CPUs will run in
	# their own batch.  Such tests just have to make do with what
	# is available.
	while (nc != ncpus) {
		batch++;
		nc = ncpus;

		# Each pass through the following loop considers one
		# test for inclusion in the current batch.
		for (i = 0; i < njobs; i++) {
			if (done[i])
				continue; # Already part of a batch.
			if (nc >= cpus[i] || nc == ncpus) {

				# This test fits into the current batch.
				done[i] = batch;
				nc -= cpus[i];
				if (nc <= 0)
					break; # Too-big test in its own batch.
			}
		}
	}

	# Dump out the tests in batch order.
	for (b = 1; b <= batch; b++)
		for (i = 0; i < njobs; i++)
			if (done[i] == b)
				print cf[i], cpus[i];
}'

# Generate a script to execute the tests in appropriate batches.
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

# Dump out the scripting required to run one test batch.
function dump(first, pastlast)
{
	print "echo ----Start batch: `date`"
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
		if (cpusr[jn] > ncpus && ncpus != 0)
			ovf = "(!)";
		else
			ovf = "";
		print "echo ", cfr[jn], cpusr[jn] ovf ": Starting build. `date`";
		print "rm -f " builddir ".*";
		print "touch " builddir ".wait";
		print "mkdir " builddir " > /dev/null 2>&1 || :";
		print "mkdir " rd cfr[jn] " || :";
		print "kvm-test-1-run.sh " CONFIGDIR cf[j], builddir, rd cfr[jn], dur " \"" RCU_QEMU_ARG "\" \"" RCU_BOOTARGS "\" > " builddir ".out 2>&1 &"
		print "echo ", cfr[jn], cpusr[jn] ovf ": Waiting for build to complete. `date`"
		print "while test -f " builddir ".wait"
		print "do"
		print "\tsleep 1"
		print "done"
		print "echo ", cfr[jn], cpusr[jn] ovf ": Build complete. `date`"
		jn++;
	}
	for (j = 1; j < jn; j++) {
		builddir=KVM "/b" j
		print "rm -f " builddir ".ready"
		print "echo ----", cfr[j], cpusr[j] ovf ": Starting kernel. `date`"
	}
	print "wait"
	print "echo ---- All kernel runs complete. `date`"
	for (j = 1; j < jn; j++) {
		builddir=KVM "/b" j
		print "echo ----", cfr[j], cpusr[j] ovf ": Build/run results:"
		print "cat " builddir ".out"
	}
}

END {
	njobs = i;
	nc = ncpus;
	first = 0;

	# Each pass through the following loop considers one test.
	for (i = 0; i < njobs; i++) {
		if (ncpus == 0) {
			# Sequential test specified, each test its own batch.
			dump(i, i + 1);
			first = i;
		} else if (nc < cpus[i] && i != 0) {
			# Out of CPUs, dump out a batch.
			dump(first, i);
			first = i;
			nc = ncpus;
		}
		# Account for the CPUs needed by the current test.
		nc -= cpus[i];
	}
	# Dump the last batch.
	if (ncpus != 0)
		dump(first, i);
}' > $T/script

if test "$dryrun" = script
then
	# Dump out the script, but define the environment variables that
	# it needs to run standalone.
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
	echo "mkdir -p "$resdir" || :"
	echo "mkdir $resdir/$ds"
	cat $T/script
	exit 0
elif test "$dryrun" = sched
then
	# Extract the test run schedule from the script.
	egrep 'start batch|Starting build\.' $T/script |
		sed -e 's/:.*$//' -e 's/^echo //'
	exit 0
else
	# Not a dryru, so run the script.
	sh $T/script
fi

# Tracing: trace_event=rcu:rcu_grace_period,rcu:rcu_future_grace_period,rcu:rcu_grace_period_init,rcu:rcu_nocb_wake,rcu:rcu_preempt_task,rcu:rcu_unlock_preempted_task,rcu:rcu_quiescent_state_report,rcu:rcu_fqs,rcu:rcu_callback,rcu:rcu_kfree_callback,rcu:rcu_batch_start,rcu:rcu_invoke_callback,rcu:rcu_invoke_kfree_callback,rcu:rcu_batch_end,rcu:rcu_torture_read,rcu:rcu_barrier

echo
echo
echo " --- `date` Test summary:"
echo Results directory: $resdir/$ds
kvm-recheck.sh $resdir/$ds
