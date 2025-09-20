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

T="`mktemp -d ${TMPDIR-/tmp}/kvm.sh.XXXXXX`"
trap 'rm -rf $T' 0

cd `dirname $scriptname`/../../../../../

# This script knows only English.
LANG=en_US.UTF-8; export LANG

dur=$((30*60))
dryrun=""
RCUTORTURE="`pwd`/tools/testing/selftests/rcutorture"; export RCUTORTURE
PATH=${RCUTORTURE}/bin:$PATH; export PATH
. functions.sh

TORTURE_ALLOTED_CPUS="`identify_qemu_vcpus`"
TORTURE_DEFCONFIG=defconfig
TORTURE_BOOT_IMAGE=""
TORTURE_BUILDONLY=
TORTURE_INITRD="$RCUTORTURE/initrd"; export TORTURE_INITRD
TORTURE_KCONFIG_ARG=""
TORTURE_KCONFIG_GDB_ARG=""
TORTURE_BOOT_GDB_ARG=""
TORTURE_QEMU_GDB_ARG=""
TORTURE_JITTER_START=""
TORTURE_JITTER_STOP=""
TORTURE_KCONFIG_KASAN_ARG=""
TORTURE_KCONFIG_KCSAN_ARG=""
TORTURE_KMAKE_ARG=""
TORTURE_NO_AFFINITY=""
TORTURE_QEMU_MEM=512
torture_qemu_mem_default=1
TORTURE_REMOTE=
TORTURE_SHUTDOWN_GRACE=180
TORTURE_SUITE=rcu
TORTURE_MOD=rcutorture
TORTURE_TRUST_MAKE=""
debuginfo="CONFIG_DEBUG_INFO_NONE=n CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y"
resdir=""
configs=""
cpus=0
ds=`date +%Y.%m.%d-%H.%M.%S`
jitter="-1"

startdate="`date`"
starttime="`get_starttime`"

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
	echo "       --debug-info"
	echo "       --dryrun batches|scenarios|sched|script"
	echo "       --duration minutes | <seconds>s | <hours>h | <days>d"
	echo "       --gdb"
	echo "       --help"
	echo "       --interactive"
	echo "       --jitter N [ maxsleep (us) [ maxspin (us) ] ]"
	echo "       --kasan"
	echo "       --kconfig Kconfig-options"
	echo "       --kcsan"
	echo "       --kmake-arg kernel-make-arguments"
	echo "       --mac nn:nn:nn:nn:nn:nn"
	echo "       --memory megabytes|nnnG"
	echo "       --no-affinity"
	echo "       --no-initrd"
	echo "       --qemu-args qemu-arguments"
	echo "       --qemu-cmd qemu-system-..."
	echo "       --remote"
	echo "       --results absolute-pathname"
	echo "       --shutdown-grace seconds"
	echo "       --torture lock|rcu|rcuscale|refscale|scf|X*"
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
		TORTURE_BOOTARGS="$TORTURE_BOOTARGS $2"
		shift
		;;
	--bootimage)
		checkarg --bootimage "(relative path to kernel boot image)" "$#" "$2" '[a-zA-Z0-9][a-zA-Z0-9_]*' '^--'
		TORTURE_BOOT_IMAGE="$2"
		shift
		;;
	--buildonly|--build-only)
		TORTURE_BUILDONLY=1
		;;
	--configs|--config)
		checkarg --configs "(list of config files)" "$#" "$2" '^[^/.a-z]\+$' '^--'
		configs="$configs $2"
		shift
		;;
	--cpus)
		checkarg --cpus "(number)" "$#" "$2" '^[0-9]*$' '^--'
		cpus=$2
		TORTURE_ALLOTED_CPUS="$2"
		if test -z "$TORTURE_REMOTE"
		then
			max_cpus="`identify_qemu_vcpus`"
			if test "$TORTURE_ALLOTED_CPUS" -gt "$max_cpus"
			then
				TORTURE_ALLOTED_CPUS=$max_cpus
			fi
		fi
		shift
		;;
	--datestamp)
		checkarg --datestamp "(relative pathname)" "$#" "$2" '^[a-zA-Z0-9._/-]*$' '^--'
		ds=$2
		shift
		;;
	--debug-info|--debuginfo)
		if test -z "$TORTURE_KCONFIG_KCSAN_ARG" && test -z "$TORTURE_BOOT_GDB_ARG"
		then
			TORTURE_KCONFIG_KCSAN_ARG="$debuginfo"; export TORTURE_KCONFIG_KCSAN_ARG
			TORTURE_BOOT_GDB_ARG="nokaslr"; export TORTURE_BOOT_GDB_ARG
		else
			echo "Ignored redundant --debug-info (implied by --kcsan &c)"
		fi
		;;
	--defconfig)
		checkarg --defconfig "defconfigtype" "$#" "$2" '^[^/][^/]*$' '^--'
		TORTURE_DEFCONFIG=$2
		shift
		;;
	--dryrun)
		checkarg --dryrun "batches|sched|script" $# "$2" 'batches\|scenarios\|sched\|script' '^--'
		dryrun=$2
		shift
		;;
	--duration)
		checkarg --duration "(minutes)" $# "$2" '^[0-9][0-9]*\(s\|m\|h\|d\|\)$' '^error'
		mult=60
		if echo "$2" | grep -q 's$'
		then
			mult=1
		elif echo "$2" | grep -q 'h$'
		then
			mult=3600
		elif echo "$2" | grep -q 'd$'
		then
			mult=86400
		fi
		ts=`echo $2 | sed -e 's/[smhd]$//'`
		dur=$(($ts*mult))
		shift
		;;
	--gdb)
		TORTURE_KCONFIG_GDB_ARG="$debuginfo"; export TORTURE_KCONFIG_GDB_ARG
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
	--kasan)
		TORTURE_KCONFIG_KASAN_ARG="$debuginfo CONFIG_KASAN=y"; export TORTURE_KCONFIG_KASAN_ARG
		if test -n "$torture_qemu_mem_default"
		then
			TORTURE_QEMU_MEM=2G
		fi
		;;
	--kconfig|--kconfigs)
		checkarg --kconfig "(Kconfig options)" $# "$2" '^\(#CHECK#\)\?CONFIG_[A-Z0-9_]\+=\([ynm]\|[0-9]\+\|"[^"]*"\)\( \+\(#CHECK#\)\?CONFIG_[A-Z0-9_]\+=\([ynm]\|[0-9]\+\|"[^"]*"\)\)* *$' '^error$'
		TORTURE_KCONFIG_ARG="`echo "$TORTURE_KCONFIG_ARG $2" | sed -e 's/^ *//' -e 's/ *$//'`"
		shift
		;;
	--kcsan)
		TORTURE_KCONFIG_KCSAN_ARG="$debuginfo CONFIG_KCSAN=y CONFIG_KCSAN_STRICT=y CONFIG_KCSAN_REPORT_ONCE_IN_MS=100000 CONFIG_KCSAN_VERBOSE=y CONFIG_DEBUG_LOCK_ALLOC=y CONFIG_PROVE_LOCKING=y"; export TORTURE_KCONFIG_KCSAN_ARG
		;;
	--kmake-arg|--kmake-args)
		checkarg --kmake-arg "(kernel make arguments)" $# "$2" '.*' '^error$'
		TORTURE_KMAKE_ARG="`echo "$TORTURE_KMAKE_ARG $2" | sed -e 's/^ *//' -e 's/ *$//'`"
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
		torture_qemu_mem_default=
		shift
		;;
	--no-affinity)
		TORTURE_NO_AFFINITY="no-affinity"
		;;
	--no-initrd)
		TORTURE_INITRD=""; export TORTURE_INITRD
		;;
	--qemu-args|--qemu-arg)
		checkarg --qemu-args "(qemu arguments)" $# "$2" '^-' '^error'
		TORTURE_QEMU_ARG="`echo "$TORTURE_QEMU_ARG $2" | sed -e 's/^ *//' -e 's/ *$//'`"
		shift
		;;
	--qemu-cmd)
		checkarg --qemu-cmd "(qemu-system-...)" $# "$2" 'qemu-system-' '^--'
		TORTURE_QEMU_CMD="$2"
		shift
		;;
	--remote)
		TORTURE_REMOTE=1
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
		checkarg --torture "(suite name)" "$#" "$2" '^\(lock\|rcu\|rcuscale\|refscale\|scf\|X.*\)$' '^--'
		TORTURE_SUITE=$2
		TORTURE_MOD="`echo $TORTURE_SUITE | sed -e 's/^\(lock\|rcu\|scf\)$/\1torture/'`"
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

if test -n "$dryrun" || test -z "$TORTURE_INITRD" || tools/testing/selftests/rcutorture/bin/mkinitrd.sh
then
	:
else
	echo No initrd and unable to create one, aborting test >&2
	exit 1
fi

CONFIGFRAG=${RCUTORTURE}/configs/${TORTURE_SUITE}; export CONFIGFRAG

defaultconfigs="`tr '\012' ' ' < $CONFIGFRAG/CFLIST`"
if test -z "$configs"
then
	configs=$defaultconfigs
fi

if test -z "$resdir"
then
	resdir=$RCUTORTURE/res
fi

# Create a file of test-name/#cpus pairs, sorted by decreasing #cpus.
configs_derep=
for CF in $configs
do
	case $CF in
	[0-9]\**|[0-9][0-9]\**|[0-9][0-9][0-9]\**|[0-9][0-9][0-9][0-9]\**)
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
echo 'BEGIN {' > $T/cfgcpu.awk
for CF1 in `echo $configs_derep | tr -s ' ' '\012' | sort -u`
do
	if test -f "$CONFIGFRAG/$CF1"
	then
		if echo "$TORTURE_KCONFIG_ARG" | grep -q '\<CONFIG_NR_CPUS='
		then
			echo "$TORTURE_KCONFIG_ARG" | tr -s ' ' | tr ' ' '\012' > $T/KCONFIG_ARG
			cpu_count=`configNR_CPUS.sh $T/KCONFIG_ARG`
		else
			cpu_count=`configNR_CPUS.sh $CONFIGFRAG/$CF1`
		fi
		cpu_count=`configfrag_boot_cpus "$TORTURE_BOOTARGS" "$CONFIGFRAG/$CF1" "$cpu_count"`
		cpu_count=`configfrag_boot_maxcpus "$TORTURE_BOOTARGS" "$CONFIGFRAG/$CF1" "$cpu_count"`
		echo 'scenariocpu["'"$CF1"'"] = '"$cpu_count"';' >> $T/cfgcpu.awk
	else
		echo "The --configs file $CF1 does not exist, terminating."
		exit 1
	fi
done
cat << '___EOF___' >> $T/cfgcpu.awk
}
{
	for (i = 1; i <= NF; i++)
		print $i, scenariocpu[$i];
}
___EOF___
echo $configs_derep | awk -f $T/cfgcpu.awk > $T/cfgcpu
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

	# Each pass through the following loop creates on test batch that
	# can be executed concurrently given ncpus.  Note that a given test
	# that requires more than the available CPUs will run in its own
	# batch.  Such tests just have to make do with what is available.
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
RCUTORTURE="$RCUTORTURE"; export RCUTORTURE
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
TORTURE_MOD="$TORTURE_MOD"; export TORTURE_MOD
TORTURE_NO_AFFINITY="$TORTURE_NO_AFFINITY"; export TORTURE_NO_AFFINITY
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
mkdir -p $resdir/$ds
TORTURE_RESDIR="$resdir/$ds"; export TORTURE_RESDIR
TORTURE_STOPFILE="$resdir/$ds/STOP.1"; export TORTURE_STOPFILE
echo Results directory: $resdir/$ds
echo $scriptname $args
touch $resdir/$ds/log
echo $scriptname $args >> $resdir/$ds/log
echo ${TORTURE_SUITE} > $resdir/$ds/torture_suite
mktestid.sh $resdir/$ds
___EOF___
kvm-assign-cpus.sh /sys/devices/system/node > $T/cpuarray.awk
kvm-get-cpus-script.sh $T/cpuarray.awk $T/dumpbatches.awk
cat << '___EOF___' >> $T/dumpbatches.awk
BEGIN {
	i = 0;
}

{
	cf[i] = $1;
	cpus[i] = $2;
	i++;
}

# Dump out the scripting required to run one test batch.
function dump(first, pastlast, batchnum,  affinitylist)
{
	print "echo ----Start batch " batchnum ": `date` | tee -a " rd "log";
	print "needqemurun="
	jn=1
	njitter = 0;
	split(jitter, ja);
	if (ja[1] == -1 && ncpus == 0)
		njitter = 1;
	else if (ja[1] == -1)
		njitter = ncpus;
	else
		njitter = ja[1];
	print "TORTURE_JITTER_START=\". jitterstart.sh " njitter " " rd " " dur " " ja[2] " " ja[3] "\"; export TORTURE_JITTER_START";
	print "TORTURE_JITTER_STOP=\". jitterstop.sh " rd " \"; export TORTURE_JITTER_STOP"
	for (j = first; j < pastlast; j++) {
		cpusr[jn] = cpus[j];
		if (cfrep[cf[j]] == "") {
			cfr[jn] = cf[j];
			cfrep[cf[j]] = 1;
		} else {
			cfrep[cf[j]]++;
			cfr[jn] = cf[j] "." cfrep[cf[j]];
		}
		builddir=rd cfr[jn] "/build";
		if (cpusr[jn] > ncpus && ncpus != 0)
			ovf = "-ovf";
		else
			ovf = "";
		print "echo ", cfr[jn], cpusr[jn] ovf ": Starting build. `date` | tee -a " rd "log";
		print "mkdir " rd cfr[jn] " || :";
		print "touch " builddir ".wait";
		affinitylist = "";
		if (gotcpus()) {
			affinitylist = nextcpus(cpusr[jn]);
		}
		if (affinitylist ~ /^[0-9,-][0-9,-]*$/)
			print "export TORTURE_AFFINITY=" affinitylist;
		else
			print "export TORTURE_AFFINITY=";
		print "kvm-test-1-run.sh " CONFIGDIR cf[j], rd cfr[jn], dur " \"" TORTURE_QEMU_ARG "\" \"" TORTURE_BOOTARGS "\" > " rd cfr[jn]  "/kvm-test-1-run.sh.out 2>&1 &"
		print "echo ", cfr[jn], cpusr[jn] ovf ": Waiting for build to complete. `date` | tee -a " rd "log";
		print "while test -f " builddir ".wait"
		print "do"
		print "\tsleep 1"
		print "done"
		print "echo ", cfr[jn], cpusr[jn] ovf ": Build complete. `date` | tee -a " rd "log";
		jn++;
	}
	print "runfiles="
	for (j = 1; j < jn; j++) {
		builddir=rd cfr[j] "/build";
		if (TORTURE_BUILDONLY)
			print "rm -f " builddir ".ready"
		else
			print "mv " builddir ".ready " builddir ".run"
			print "runfiles=\"$runfiles " builddir ".run\""
		fi
		print "if test -f \"" rd cfr[j] "/builtkernel\""
		print "then"
		print "\techo ----", cfr[j], cpusr[j] ovf ": Kernel present. `date` | tee -a " rd "log";
		print "\tneedqemurun=1"
		print "fi"
	}
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
	print "\t$TORTURE_JITTER_START";
	print "\twhile ls $runfiles > /dev/null 2>&1"
	print "\tdo"
	print "\t\t:"
	print "\tdone"
	print "\t$TORTURE_JITTER_STOP";
	print "\techo ---- All kernel runs complete. `date` | tee -a " rd "log";
	print "else"
	print "\twait"
	print "\techo ---- No kernel runs. `date` | tee -a " rd "log";
	print "fi"
	for (j = 1; j < jn; j++) {
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
}
___EOF___
awk < $T/cfgcpu.pack \
	-v TORTURE_BUILDONLY="$TORTURE_BUILDONLY" \
	-v CONFIGDIR="$CONFIGFRAG/" \
	-v RCUTORTURE="$RCUTORTURE" \
	-v ncpus=$cpus \
	-v jitter="$jitter" \
	-v rd=$resdir/$ds/ \
	-v dur=$dur \
	-v TORTURE_QEMU_ARG="$TORTURE_QEMU_ARG" \
	-v TORTURE_BOOTARGS="$TORTURE_BOOTARGS" \
	-f $T/dumpbatches.awk >> $T/script
echo kvm-end-run-stats.sh "$resdir/$ds" "$starttime" >> $T/script

# Extract the tests and their batches from the script.
grep -E 'Start batch|Starting build\.' $T/script | grep -v ">>" |
	sed -e 's/:.*$//' -e 's/^echo //' -e 's/-ovf//' |
	awk '
	/^----Start/ {
		batchno = $3;
		next;
	}
	{
		print batchno, $1, $2
	}' > $T/batches

# As above, but one line per batch.
grep -v '^#' $T/batches | awk '
BEGIN {
	oldbatch = 1;
}

{
	if (oldbatch != $1) {
		print ++n ". " curbatch;
		curbatch = "";
		oldbatch = $1;
	}
	curbatch = curbatch " " $2;
}

END {
	print ++n ". " curbatch;
}' > $T/scenarios

if test "$dryrun" = script
then
	cat $T/script
	exit 0
elif test "$dryrun" = sched
then
	# Extract the test run schedule from the script.
	grep -E 'Start batch|Starting build\.' $T/script | grep -v ">>" |
		sed -e 's/:.*$//' -e 's/^echo //'
	nbuilds="`grep 'Starting build\.' $T/script |
		  grep -v ">>" | sed -e 's/:.*$//' -e 's/^echo //' |
		  awk '{ print $1 }' | grep -v '\.' | wc -l`"
	echo Total number of builds: $nbuilds
	nbatches="`grep 'Start batch' $T/script | grep -v ">>" | wc -l`"
	echo Total number of batches: $nbatches
	exit 0
elif test "$dryrun" = batches
then
	cat $T/batches
	exit 0
elif test "$dryrun" = scenarios
then
	cat $T/scenarios
	exit 0
else
	# Not a dryrun.  Record the batches and the number of CPUs, then run the script.
	bash $T/script
	ret=$?
	cp $T/batches $resdir/$ds/batches
	cp $T/scenarios $resdir/$ds/scenarios
	echo '#' cpus=$cpus >> $resdir/$ds/batches
	exit $ret
fi

# Tracing: trace_event=rcu:rcu_grace_period,rcu:rcu_future_grace_period,rcu:rcu_grace_period_init,rcu:rcu_nocb_wake,rcu:rcu_preempt_task,rcu:rcu_unlock_preempted_task,rcu:rcu_quiescent_state_report,rcu:rcu_fqs,rcu:rcu_callback,rcu:rcu_kfree_callback,rcu:rcu_batch_start,rcu:rcu_invoke_callback,rcu:rcu_invoke_kfree_callback,rcu:rcu_batch_end,rcu:rcu_torture_read,rcu:rcu_barrier
# Function-graph tracing: ftrace=function_graph ftrace_graph_filter=sched_setaffinity,migration_cpu_stop
# Also --kconfig "CONFIG_FUNCTION_TRACER=y CONFIG_FUNCTION_GRAPH_TRACER=y"
# Control buffer size: --bootargs trace_buf_size=3k
# Get trace-buffer dumps on all oopses: --bootargs ftrace_dump_on_oops
# Ditto, but dump only the oopsing CPU: --bootargs ftrace_dump_on_oops=orig_cpu
# Heavy-handed way to also dump on warnings: --bootargs panic_on_warn=1
