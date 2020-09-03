#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Run a series of tests under KVM.  By default, this series is specified
# by the relevant CFLIST file, but can be overridden by the --configs
# command-line argument.
#
# Usage: kvm.sh [ options ]
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

scriptname=$0
args="$*"

T=${TMPDIR-/tmp}/kvm.sh.$$
trap 'rm -rf $T' 0
mkdir $T

cd `dirname $scriptname`/../../../../../

dur=$((30*60))
dryrun=""
KVM="`pwd`/tools/testing/selftests/rcutorture"; export KVM
PATH=${KVM}/bin:$PATH; export PATH
. functions.sh

TORTURE_ALLOTED_CPUS="`identify_qemu_vcpus`"
TORTURE_DEFCONFIG=defconfig
TORTURE_BOOT_IMAGE=""
TORTURE_INITRD="$KVM/initrd"; export TORTURE_INITRD
TORTURE_KCONFIG_ARG=""
TORTURE_KCONFIG_GDB_ARG=""
TORTURE_BOOT_GDB_ARG=""
TORTURE_QEMU_GDB_ARG=""
TORTURE_KCONFIG_KASAN_ARG=""
TORTURE_KCONFIG_KCSAN_ARG=""
TORTURE_KMAKE_ARG=""
TORTURE_QEMU_MEM=512
TORTURE_SHUTDOWN_GRACE=180
TORTURE_SUITE=rcu
TORTURE_TRUST_MAKE=""
resdir=""
configs=""
cpus=0
ds=`date +%Y.%m.%d-%H.%M.%S`
jitter="-1"

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --allcpus"
	echo "       --bootargs kernel-boot-arguments"
	echo "       --bootimage relative-path-to-kernel-boot-image"
	echo "       --buildonly"
	echo "       --configs \"config-file list w/ repeat factor (3*TINY01)\""
	echo "       --cpus N"
	echo "       --datestamp string"
	echo "       --defconfig string"
	echo "       --dryrun sched|script"
	echo "       --duration minutes"
	echo "       --gdb"
	echo "       --help"
	echo "       --interactive"
	echo "       --jitter N [ maxsleep (us) [ maxspin (us) ] ]"
	echo "       --kconfig Kconfig-options"
	echo "       --kmake-arg kernel-make-arguments"
	echo "       --mac nn:nn:nn:nn:nn:nn"
	echo "       --memory megabytes|nnnG"
	echo "       --no-initrd"
	echo "       --qemu-args qemu-arguments"
	echo "       --qemu-cmd qemu-system-..."
	echo "       --results absolute-pathname"
	echo "       --torture lock|rcu|rcuscale|refscale|scf"
	echo "       --trust-make"
	exit 1
}

while test $# -gt 0
do
	case "$1" in
	--allcpus)
		cpus=$TORTURE_ALLOTED_CPUS
		max_cpus=$TORTURE_ALLOTED_CPUS
		;;
	--bootargs|--bootarg)
		checkarg --bootargs "(list of kernel boot arguments)" "$#" "$2" '.*' '^--'
		TORTURE_BOOTARGS="$2"
		shift
		;;
	--bootimage)
		checkarg --bootimage "(relative path to kernel boot image)" "$#" "$2" '[a-zA-Z0-9][a-zA-Z0-9_]*' '^--'
		TORTURE_BOOT_IMAGE="$2"
		shift
		;;
	--buildonly)
		TORTURE_BUILDONLY=1
		;;
	--configs|--config)
		checkarg --configs "(list of config files)" "$#" "$2" '^[^/]*$' '^--'
		configs="$2"
		shift
		;;
	--cpus)
		checkarg --cpus "(number)" "$#" "$2" '^[0-9]*$' '^--'
		cpus=$2
		TORTURE_ALLOTED_CPUS="$2"
		max_cpus="`identify_qemu_vcpus`"
		if test "$TORTURE_ALLOTED_CPUS" -gt "$max_cpus"
		then
			TORTURE_ALLOTED_CPUS=$max_cpus
		fi
		shift
		;;
	--datestamp)
		checkarg --datestamp "(relative pathname)" "$#" "$2" '^[^/]*$' '^--'
		ds=$2
		shift
		;;
	--defconfig)
		checkarg --defconfig "defconfigtype" "$#" "$2" '^[^/][^/]*$' '^--'
		TORTURE_DEFCONFIG=$2
		shift
		;;
	--dryrun)
		checkarg --dryrun "sched|script" $# "$2" 'sched\|script' '^--'
		dryrun=$2
		shift
		;;
	--duration)
		checkarg --duration "(minutes)" $# "$2" '^[0-9]*$' '^error'
		dur=$(($2*60))
		shift
		;;
	--gdb)
		TORTURE_KCONFIG_GDB_ARG="CONFIG_DEBUG_INFO=y"; export TORTURE_KCONFIG_GDB_ARG
		TORTURE_BOOT_GDB_ARG="nokaslr"; export TORTURE_BOOT_GDB_ARG
		TORTURE_QEMU_GDB_ARG="-s -S"; export TORTURE_QEMU_GDB_ARG
		;;
	--help|-h)
		usage
		;;
	--interactive)
		TORTURE_QEMU_INTERACTIVE=1; export TORTURE_QEMU_INTERACTIVE
		;;
	--jitter)
		checkarg --jitter "(# threads [ sleep [ spin ] ])" $# "$2" '^-\{,1\}[0-9]\+\( \+[0-9]\+\)\{,2\} *$' '^error$'
		jitter="$2"
		shift
		;;
	--kconfig)
		checkarg --kconfig "(Kconfig options)" $# "$2" '^CONFIG_[A-Z0-9_]\+=\([ynm]\|[0-9]\+\)\( CONFIG_[A-Z0-9_]\+=\([ynm]\|[0-9]\+\)\)*$' '^error$'
		TORTURE_KCONFIG_ARG="$2"
		shift
		;;
	--kasan)
		TORTURE_KCONFIG_KASAN_ARG="CONFIG_DEBUG_INFO=y CONFIG_KASAN=y"; export TORTURE_KCONFIG_KASAN_ARG
		;;
	--kcsan)
		TORTURE_KCONFIG_KCSAN_ARG="CONFIG_DEBUG_INFO=y CONFIG_KCSAN=y CONFIG_KCSAN_ASSUME_PLAIN_WRITES_ATOMIC=n CONFIG_KCSAN_REPORT_VALUE_CHANGE_ONLY=n CONFIG_KCSAN_REPORT_ONCE_IN_MS=100000 CONFIG_KCSAN_VERBOSE=y CONFIG_KCSAN_INTERRUPT_WATCHER=y"; export TORTURE_KCONFIG_KCSAN_ARG
		;;
	--kmake-arg)
		checkarg --kmake-arg "(kernel make arguments)" $# "$2" '.*' '^error$'
		TORTURE_KMAKE_ARG="$2"
		shift
		;;
	--mac)
		checkarg --mac "(MAC address)" $# "$2" '^\([0-9a-fA-F]\{2\}:\)\{5\}[0-9a-fA-F]\{2\}$' error
		TORTURE_QEMU_MAC=$2
		shift
		;;
	--memory)
		checkarg --memory "(memory size)" $# "$2" '^[0-9]\+[MG]\?$' error
		TORTURE_QEMU_MEM=$2
		shift
		;;
	--no-initrd)
		TORTURE_INITRD=""; export TORTURE_INITRD
		;;
	--qemu-args|--qemu-arg)
		checkarg --qemu-args "(qemu arguments)" $# "$2" '^-' '^error'
		TORTURE_QEMU_ARG="$2"
		shift
		;;
	--qemu-cmd)
		checkarg --qemu-cmd "(qemu-system-...)" $# "$2" 'qemu-system-' '^--'
		TORTURE_QEMU_CMD="$2"
		shift
		;;
	--results)
		checkarg --results "(absolute pathname)" "$#" "$2" '^/' '^error'
		resdir=$2
		shift
		;;
	--shutdown-grace)
		checkarg --shutdown-grace "(seconds)" "$#" "$2" '^[0-9]*$' '^error'
		TORTURE_SHUTDOWN_GRACE=$2
		shift
		;;
	--torture)
		checkarg --torture "(suite name)" "$#" "$2" '^\(lock\|rcu\|rcuscale\|refscale\|scf\)$' '^--'
		TORTURE_SUITE=$2
		shift
		if test "$TORTURE_SUITE" = rcuscale || test "$TORTURE_SUITE" = refscale
		then
			# If you really want jitter for refscale or
			# rcuscale, specify it after specifying the rcuscale
			# or the refscale.  (But why jitter in these cases?)
			jitter=0
		fi
		;;
	--trust-make)
		TORTURE_TRUST_MAKE="y"
		;;
	*)
		echo Unknown argument $1
		usage
		;;
	esac
	shift
done

if test -z "$TORTURE_INITRD" || tools/testing/selftests/rcutorture/bin/mkinitrd.sh
then
	:
else
	echo No initrd and unable to create one, aborting test >&2
	exit 1
fi

CONFIGFRAG=${KVM}/configs/${TORTURE_SUITE}; export CONFIGFRAG

defaultconfigs="`tr '\012' ' ' < $CONFIGFRAG/CFLIST`"
if test -z "$configs"
then
	configs=$defaultconfigs
fi

if test -z "$resdir"
then
	resdir=$KVM/res
fi

# Create a file of test-name/#cpus pairs, sorted by decreasing #cpus.
configs_derep=
for CF in $configs
do
	case $CF in
	[0-9]\**|[0-9][0-9]\**|[0-9][0-9][0-9]\**)
		config_reps=`echo $CF | sed -e 's/\*.*$//'`
		CF1=`echo $CF | sed -e 's/^[^*]*\*//'`
		;;
	*)
		config_reps=1
		CF1=$CF
		;;
	esac
	for ((cur_rep=0;cur_rep<$config_reps;cur_rep++))
	do
		configs_derep="$configs_derep $CF1"
	done
done
touch $T/cfgcpu
configs_derep="`echo $configs_derep | sed -e "s/\<CFLIST\>/$defaultconfigs/g"`"
if test -n "$TORTURE_KCONFIG_GDB_ARG"
then
	if test "`echo $configs_derep | wc -w`" -gt 1
	then
		echo "The --config list is: $configs_derep."
		echo "Only one --config permitted with --gdb, terminating."
		exit 1
	fi
fi
for CF1 in $configs_derep
do
	if test -f "$CONFIGFRAG/$CF1"
	then
		cpu_count=`configNR_CPUS.sh $CONFIGFRAG/$CF1`
		cpu_count=`configfrag_boot_cpus "$TORTURE_BOOTARGS" "$CONFIGFRAG/$CF1" "$cpu_count"`
		cpu_count=`configfrag_boot_maxcpus "$TORTURE_BOOTARGS" "$CONFIGFRAG/$CF1" "$cpu_count"`
		echo $CF1 $cpu_count >> $T/cfgcpu
	else
		echo "The --configs file $CF1 does not exist, terminating."
		exit 1
	fi
done
sort -k2nr $T/cfgcpu -T="$T" > $T/cfgcpu.sort

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
cat << ___EOF___ > $T/script
CONFIGFRAG="$CONFIGFRAG"; export CONFIGFRAG
KVM="$KVM"; export KVM
PATH="$PATH"; export PATH
TORTURE_ALLOTED_CPUS="$TORTURE_ALLOTED_CPUS"; export TORTURE_ALLOTED_CPUS
TORTURE_BOOT_IMAGE="$TORTURE_BOOT_IMAGE"; export TORTURE_BOOT_IMAGE
TORTURE_BUILDONLY="$TORTURE_BUILDONLY"; export TORTURE_BUILDONLY
TORTURE_DEFCONFIG="$TORTURE_DEFCONFIG"; export TORTURE_DEFCONFIG
TORTURE_INITRD="$TORTURE_INITRD"; export TORTURE_INITRD
TORTURE_KCONFIG_ARG="$TORTURE_KCONFIG_ARG"; export TORTURE_KCONFIG_ARG
TORTURE_KCONFIG_GDB_ARG="$TORTURE_KCONFIG_GDB_ARG"; export TORTURE_KCONFIG_GDB_ARG
TORTURE_BOOT_GDB_ARG="$TORTURE_BOOT_GDB_ARG"; export TORTURE_BOOT_GDB_ARG
TORTURE_QEMU_GDB_ARG="$TORTURE_QEMU_GDB_ARG"; export TORTURE_QEMU_GDB_ARG
TORTURE_KCONFIG_KASAN_ARG="$TORTURE_KCONFIG_KASAN_ARG"; export TORTURE_KCONFIG_KASAN_ARG
TORTURE_KCONFIG_KCSAN_ARG="$TORTURE_KCONFIG_KCSAN_ARG"; export TORTURE_KCONFIG_KCSAN_ARG
TORTURE_KMAKE_ARG="$TORTURE_KMAKE_ARG"; export TORTURE_KMAKE_ARG
TORTURE_QEMU_CMD="$TORTURE_QEMU_CMD"; export TORTURE_QEMU_CMD
TORTURE_QEMU_INTERACTIVE="$TORTURE_QEMU_INTERACTIVE"; export TORTURE_QEMU_INTERACTIVE
TORTURE_QEMU_MAC="$TORTURE_QEMU_MAC"; export TORTURE_QEMU_MAC
TORTURE_QEMU_MEM="$TORTURE_QEMU_MEM"; export TORTURE_QEMU_MEM
TORTURE_SHUTDOWN_GRACE="$TORTURE_SHUTDOWN_GRACE"; export TORTURE_SHUTDOWN_GRACE
TORTURE_SUITE="$TORTURE_SUITE"; export TORTURE_SUITE
TORTURE_TRUST_MAKE="$TORTURE_TRUST_MAKE"; export TORTURE_TRUST_MAKE
if ! test -e $resdir
then
	mkdir -p "$resdir" || :
fi
mkdir $resdir/$ds
TORTURE_RESDIR="$resdir/$ds"; export TORTURE_RESDIR
TORTURE_STOPFILE="$resdir/$ds/STOP"; export TORTURE_STOPFILE
echo Results directory: $resdir/$ds
echo $scriptname $args
touch $resdir/$ds/log
echo $scriptname $args >> $resdir/$ds/log
echo ${TORTURE_SUITE} > $resdir/$ds/TORTURE_SUITE
pwd > $resdir/$ds/testid.txt
if test -d .git
then
	git status >> $resdir/$ds/testid.txt
	git rev-parse HEAD >> $resdir/$ds/testid.txt
	git diff HEAD >> $resdir/$ds/testid.txt
fi
___EOF___
awk < $T/cfgcpu.pack \
	-v TORTURE_BUILDONLY="$TORTURE_BUILDONLY" \
	-v CONFIGDIR="$CONFIGFRAG/" \
	-v KVM="$KVM" \
	-v ncpus=$cpus \
	-v jitter="$jitter" \
	-v rd=$resdir/$ds/ \
	-v dur=$dur \
	-v TORTURE_QEMU_ARG="$TORTURE_QEMU_ARG" \
	-v TORTURE_BOOTARGS="$TORTURE_BOOTARGS" \
'BEGIN {
	i = 0;
}

{
	cf[i] = $1;
	cpus[i] = $2;
	i++;
}

# Dump out the scripting required to run one test batch.
function dump(first, pastlast, batchnum)
{
	print "echo ----Start batch " batchnum ": `date` | tee -a " rd "log";
	print "needqemurun="
	jn=1
	for (j = first; j < pastlast; j++) {
		builddir=KVM "/b" j - first + 1
		cpusr[jn] = cpus[j];
		if (cfrep[cf[j]] == "") {
			cfr[jn] = cf[j];
			cfrep[cf[j]] = 1;
		} else {
			cfrep[cf[j]]++;
			cfr[jn] = cf[j] "." cfrep[cf[j]];
		}
		if (cpusr[jn] > ncpus && ncpus != 0)
			ovf = "-ovf";
		else
			ovf = "";
		print "echo ", cfr[jn], cpusr[jn] ovf ": Starting build. `date` | tee -a " rd "log";
		print "rm -f " builddir ".*";
		print "touch " builddir ".wait";
		print "mkdir " rd cfr[jn] " || :";
		print "kvm-test-1-run.sh " CONFIGDIR cf[j], builddir, rd cfr[jn], dur " \"" TORTURE_QEMU_ARG "\" \"" TORTURE_BOOTARGS "\" > " rd cfr[jn]  "/kvm-test-1-run.sh.out 2>&1 &"
		print "echo ", cfr[jn], cpusr[jn] ovf ": Waiting for build to complete. `date` | tee -a " rd "log";
		print "while test -f " builddir ".wait"
		print "do"
		print "\tsleep 1"
		print "done"
		print "echo ", cfr[jn], cpusr[jn] ovf ": Build complete. `date` | tee -a " rd "log";
		jn++;
	}
	for (j = 1; j < jn; j++) {
		builddir=KVM "/b" j
		print "rm -f " builddir ".ready"
		print "if test -f \"" rd cfr[j] "/builtkernel\""
		print "then"
		print "\techo ----", cfr[j], cpusr[j] ovf ": Kernel present. `date` | tee -a " rd "log";
		print "\tneedqemurun=1"
		print "fi"
	}
	njitter = 0;
	split(jitter, ja);
	if (ja[1] == -1 && ncpus == 0)
		njitter = 1;
	else if (ja[1] == -1)
		njitter = ncpus;
	else
		njitter = ja[1];
	if (TORTURE_BUILDONLY && njitter != 0) {
		njitter = 0;
		print "echo Build-only run, so suppressing jitter | tee -a " rd "log"
	}
	if (TORTURE_BUILDONLY) {
		print "needqemurun="
	}
	print "if test -n \"$needqemurun\""
	print "then"
	print "\techo ---- Starting kernels. `date` | tee -a " rd "log";
	for (j = 0; j < njitter; j++)
		print "\tjitter.sh " j " " dur " " ja[2] " " ja[3] "&"
	print "\twait"
	print "\techo ---- All kernel runs complete. `date` | tee -a " rd "log";
	print "else"
	print "\twait"
	print "\techo ---- No kernel runs. `date` | tee -a " rd "log";
	print "fi"
	for (j = 1; j < jn; j++) {
		builddir=KVM "/b" j
		print "echo ----", cfr[j], cpusr[j] ovf ": Build/run results: | tee -a " rd "log";
		print "cat " rd cfr[j]  "/kvm-test-1-run.sh.out | tee -a " rd "log";
	}
}

END {
	njobs = i;
	nc = ncpus;
	first = 0;
	batchnum = 1;

	# Each pass through the following loop considers one test.
	for (i = 0; i < njobs; i++) {
		if (ncpus == 0) {
			# Sequential test specified, each test its own batch.
			dump(i, i + 1, batchnum);
			first = i;
			batchnum++;
		} else if (nc < cpus[i] && i != 0) {
			# Out of CPUs, dump out a batch.
			dump(first, i, batchnum);
			first = i;
			nc = ncpus;
			batchnum++;
		}
		# Account for the CPUs needed by the current test.
		nc -= cpus[i];
	}
	# Dump the last batch.
	if (ncpus != 0)
		dump(first, i, batchnum);
}' >> $T/script

cat << ___EOF___ >> $T/script
echo
echo
echo " --- `date` Test summary:"
echo Results directory: $resdir/$ds
kcsan-collapse.sh $resdir/$ds
kvm-recheck.sh $resdir/$ds
___EOF___

if test "$dryrun" = script
then
	cat $T/script
	exit 0
elif test "$dryrun" = sched
then
	# Extract the test run schedule from the script.
	egrep 'Start batch|Starting build\.' $T/script |
		grep -v ">>" |
		sed -e 's/:.*$//' -e 's/^echo //'
	exit 0
else
	# Not a dryrun, so run the script.
	sh $T/script
fi

# Tracing: trace_event=rcu:rcu_grace_period,rcu:rcu_future_grace_period,rcu:rcu_grace_period_init,rcu:rcu_nocb_wake,rcu:rcu_preempt_task,rcu:rcu_unlock_preempted_task,rcu:rcu_quiescent_state_report,rcu:rcu_fqs,rcu:rcu_callback,rcu:rcu_kfree_callback,rcu:rcu_batch_start,rcu:rcu_invoke_callback,rcu:rcu_invoke_kfree_callback,rcu:rcu_batch_end,rcu:rcu_torture_read,rcu:rcu_barrier
# Function-graph tracing: ftrace=function_graph ftrace_graph_filter=sched_setaffinity,migration_cpu_stop
# Also --kconfig "CONFIG_FUNCTION_TRACER=y CONFIG_FUNCTION_GRAPH_TRACER=y"
# Control buffer size: --bootargs trace_buf_size=3k
# Get trace-buffer dumps on all oopses: --bootargs ftrace_dump_on_oops
# Ditto, but dump only the oopsing CPU: --bootargs ftrace_dump_on_oops=orig_cpu
# Heavy-handed way to also dump on warnings: --bootargs panic_on_warn
