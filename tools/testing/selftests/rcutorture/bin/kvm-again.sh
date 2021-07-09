#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Rerun a series of tests under KVM.
#
# Usage: kvm-again.sh /path/to/old/run [ options ]
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

scriptname=$0
args="$*"

T=${TMPDIR-/tmp}/kvm-again.sh.$$
trap 'rm -rf $T' 0
mkdir $T

if ! test -d tools/testing/selftests/rcutorture/bin
then
	echo $scriptname must be run from top-level directory of kernel source tree.
	exit 1
fi

oldrun=$1
shift
if ! test -d "$oldrun"
then
	echo "Usage: $scriptname /path/to/old/run [ options ]"
	exit 1
fi
if ! cp "$oldrun/scenarios" $T/scenarios.oldrun
then
	# Later on, can reconstitute this from console.log files.
	echo Prior run batches file does not exist: $oldrun/batches
	exit 1
fi

if test -f "$oldrun/torture_suite"
then
	torture_suite="`cat $oldrun/torture_suite`"
elif test -f "$oldrun/TORTURE_SUITE"
then
	torture_suite="`cat $oldrun/TORTURE_SUITE`"
else
	echo "Prior run torture_suite file does not exist: $oldrun/{torture_suite,TORTURE_SUITE}"
	exit 1
fi

KVM="`pwd`/tools/testing/selftests/rcutorture"; export KVM
PATH=${KVM}/bin:$PATH; export PATH
. functions.sh

dryrun=
dur=
default_link="cp -R"
rundir="`pwd`/tools/testing/selftests/rcutorture/res/`date +%Y.%m.%d-%H.%M.%S-again`"

startdate="`date`"
starttime="`get_starttime`"

usage () {
	echo "Usage: $scriptname $oldrun [ arguments ]:"
	echo "       --dryrun"
	echo "       --duration minutes | <seconds>s | <hours>h | <days>d"
	echo "       --link hard|soft|copy"
	echo "       --remote"
	echo "       --rundir /new/res/path"
	exit 1
}

while test $# -gt 0
do
	case "$1" in
	--dryrun)
		dryrun=1
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
	--link)
		checkarg --link "hard|soft|copy" "$#" "$2" 'hard\|soft\|copy' '^--'
		case "$2" in
		copy)
			arg_link="cp -R"
			;;
		hard)
			arg_link="cp -Rl"
			;;
		soft)
			arg_link="cp -Rs"
			;;
		esac
		shift
		;;
	--remote)
		arg_remote=1
		default_link="cp -as"
		;;
	--rundir)
		checkarg --rundir "(absolute pathname)" "$#" "$2" '^/' '^error'
		rundir=$2
		if test -e "$rundir"
		then
			echo "--rundir $2: Already exists."
			usage
		fi
		shift
		;;
	*)
		echo Unknown argument $1
		usage
		;;
	esac
	shift
done
if test -z "$arg_link"
then
	arg_link="$default_link"
fi

echo ---- Re-run results directory: $rundir

# Copy old run directory tree over and adjust.
mkdir -p "`dirname "$rundir"`"
if ! $arg_link "$oldrun" "$rundir"
then
	echo "Cannot copy from $oldrun to $rundir."
	usage
fi
rm -f "$rundir"/*/{console.log,console.log.diags,qemu_pid,qemu-pid,qemu-retval,Warnings,kvm-test-1-run.sh.out,kvm-test-1-run-qemu.sh.out,vmlinux} "$rundir"/log
touch "$rundir/log"
echo $scriptname $args | tee -a "$rundir/log"
echo $oldrun > "$rundir/re-run"
if ! test -d "$rundir/../../bin"
then
	$arg_link "$oldrun/../../bin" "$rundir/../.."
fi
for i in $rundir/*/qemu-cmd
do
	cp "$i" $T
	qemu_cmd_dir="`dirname "$i"`"
	kernel_dir="`echo $qemu_cmd_dir | sed -e 's/\.[0-9]\+$//'`"
	jitter_dir="`dirname "$kernel_dir"`"
	kvm-transform.sh "$kernel_dir/bzImage" "$qemu_cmd_dir/console.log" "$jitter_dir" $dur < $T/qemu-cmd > $i
	if test -n "$arg_remote"
	then
		echo "# TORTURE_KCONFIG_GDB_ARG=''" >> $i
	fi
done

# Extract settings from the last qemu-cmd file transformed above.
grep '^#' $i | sed -e 's/^# //' > $T/qemu-cmd-settings
. $T/qemu-cmd-settings

grep -v '^#' $T/scenarios.oldrun | awk '
{
	curbatch = "";
	for (i = 2; i <= NF; i++)
		curbatch = curbatch " " $i;
	print "kvm-test-1-run-batch.sh" curbatch;
}' > $T/runbatches.sh

if test -n "$dryrun"
then
	echo ---- Dryrun complete, directory: $rundir | tee -a "$rundir/log"
else
	( cd "$rundir"; sh $T/runbatches.sh ) | tee -a "$rundir/log"
	kvm-end-run-stats.sh "$rundir" "$starttime"
fi
