#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Run a series of tests on remote systems under KVM.
#
# Usage: kvm-remote.sh "systems" [ <kvm.sh args> ]
#	 kvm-remote.sh "systems" /path/to/old/run [ <kvm-again.sh args> ]
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

scriptname=$0
args="$*"

if ! test -d tools/testing/selftests/rcutorture/bin
then
	echo $scriptname must be run from top-level directory of kernel source tree.
	exit 1
fi

RCUTORTURE="`pwd`/tools/testing/selftests/rcutorture"; export RCUTORTURE
PATH=${RCUTORTURE}/bin:$PATH; export PATH
. functions.sh

starttime="`get_starttime`"

systems="$1"
if test -z "$systems"
then
	echo $scriptname: Empty list of systems will go nowhere good, giving up.
	exit 1
fi
shift

# Pathnames:
# T:	  /tmp/kvm-remote.sh.$$
# resdir: /tmp/kvm-remote.sh.$$/res
# rundir: /tmp/kvm-remote.sh.$$/res/$ds ("-remote" suffix)
# oldrun: `pwd`/tools/testing/.../res/$otherds
#
# Pathname segments:
# TD:	  kvm-remote.sh.$$
# ds:	  yyyy.mm.dd-hh.mm.ss-remote

TD=kvm-remote.sh.$$
T=${TMPDIR-/tmp}/$TD
trap 'rm -rf $T' 0
mkdir $T

resdir="$T/res"
ds=`date +%Y.%m.%d-%H.%M.%S`-remote
rundir=$resdir/$ds
echo Results directory: $rundir
echo $scriptname $args
if echo $1 | grep -q '^--'
then
	# Fresh build.  Create a datestamp unless the caller supplied one.
	datestamp="`echo "$@" | awk -v ds="$ds" '{
		for (i = 1; i < NF; i++) {
			if ($i == "--datestamp") {
				ds = "";
				break;
			}
		}
		if (ds != "")
			print "--datestamp " ds;
	}'`"
	kvm.sh --remote "$@" $datestamp --buildonly > $T/kvm.sh.out 2>&1
	ret=$?
	if test "$ret" -ne 0
	then
		echo $scriptname: kvm.sh failed exit code $?
		cat $T/kvm.sh.out
		exit 2
	fi
	oldrun="`grep -m 1 "^Results directory: " $T/kvm.sh.out | awk '{ print $3 }'`"
	touch "$oldrun/remote-log"
	echo $scriptname $args >> "$oldrun/remote-log"
	echo | tee -a "$oldrun/remote-log"
	echo " ----" kvm.sh output: "(`date`)" | tee -a "$oldrun/remote-log"
	cat $T/kvm.sh.out | tee -a "$oldrun/remote-log"
	# We are going to run this, so remove the buildonly files.
	rm -f "$oldrun"/*/buildonly
	kvm-again.sh $oldrun --dryrun --remote --rundir "$rundir" > $T/kvm-again.sh.out 2>&1
	ret=$?
	if test "$ret" -ne 0
	then
		echo $scriptname: kvm-again.sh failed exit code $? | tee -a "$oldrun/remote-log"
		cat $T/kvm-again.sh.out | tee -a "$oldrun/remote-log"
		exit 2
	fi
else
	# Re-use old run.
	oldrun="$1"
	if ! echo $oldrun | grep -q '^/'
	then
		oldrun="`pwd`/$oldrun"
	fi
	shift
	touch "$oldrun/remote-log"
	echo $scriptname $args >> "$oldrun/remote-log"
	kvm-again.sh "$oldrun" "$@" --dryrun --remote --rundir "$rundir" > $T/kvm-again.sh.out 2>&1
	ret=$?
	if test "$ret" -ne 0
	then
		echo $scriptname: kvm-again.sh failed exit code $? | tee -a "$oldrun/remote-log"
		cat $T/kvm-again.sh.out | tee -a "$oldrun/remote-log"
		exit 2
	fi
	cp -a "$rundir" "$RCUTORTURE/res/"
	oldrun="$RCUTORTURE/res/$ds"
fi
echo | tee -a "$oldrun/remote-log"
echo " ----" kvm-again.sh output: "(`date`)" | tee -a "$oldrun/remote-log"
cat $T/kvm-again.sh.out
echo | tee -a "$oldrun/remote-log"
echo Remote run directory: $rundir | tee -a "$oldrun/remote-log"
echo Local build-side run directory: $oldrun | tee -a "$oldrun/remote-log"

# Create the kvm-remote-N.sh scripts in the bin directory.
awk < "$rundir"/scenarios -v dest="$T/bin" -v rundir="$rundir" '
{
	n = $1;
	sub(/\./, "", n);
	fn = dest "/kvm-remote-" n ".sh"
	print "kvm-remote-noreap.sh " rundir " &" > fn;
	scenarios = "";
	for (i = 2; i <= NF; i++)
		scenarios = scenarios " " $i;
	print "kvm-test-1-run-batch.sh" scenarios >> fn;
	print "sync" >> fn;
	print "rm " rundir "/remote.run" >> fn;
}'
chmod +x $T/bin/kvm-remote-*.sh
( cd "`dirname $T`"; tar -chzf $T/binres.tgz "$TD/bin" "$TD/res" )

# Check first to avoid the need for cleanup for system-name typos
for i in $systems
do
	ncpus="`ssh $i getconf _NPROCESSORS_ONLN 2> /dev/null`"
	echo $i: $ncpus CPUs " " `date` | tee -a "$oldrun/remote-log"
	ret=$?
	if test "$ret" -ne 0
	then
		echo System $i unreachable, giving up. | tee -a "$oldrun/remote-log"
		exit 4
	fi
done

# Download and expand the tarball on all systems.
echo Build-products tarball: `du -h $T/binres.tgz` | tee -a "$oldrun/remote-log"
for i in $systems
do
	echo Downloading tarball to $i `date` | tee -a "$oldrun/remote-log"
	cat $T/binres.tgz | ssh $i "cd /tmp; tar -xzf -"
	ret=$?
	tries=0
	while test "$ret" -ne 0
	do
		echo Unable to download $T/binres.tgz to system $i, waiting and then retrying.  $tries prior retries. | tee -a "$oldrun/remote-log"
		sleep 60
		cat $T/binres.tgz | ssh $i "cd /tmp; tar -xzf -"
		ret=$?
		if test "$ret" -ne 0
		then
			if test "$tries" > 5
			then
				echo Unable to download $T/binres.tgz to system $i, giving up. | tee -a "$oldrun/remote-log"
				exit 10
			fi
		fi
		tries=$((tries+1))
	done
done

# Function to check for presence of a file on the specified system.
# Complain if the system cannot be reached, and retry after a wait.
# Currently just waits forever if a machine disappears.
#
# Usage: checkremotefile system pathname
checkremotefile () {
	local ret
	local sleeptime=60

	while :
	do
		ssh $1 "test -f \"$2\""
		ret=$?
		if test "$ret" -eq 255
		then
			echo " ---" ssh failure to $1 checking for file $2, retry after $sleeptime seconds. `date` | tee -a "$oldrun/remote-log"
		elif test "$ret" -eq 0
		then
			return 0
		elif test "$ret" -eq 1
		then
			echo " ---" File \"$2\" not found: ssh $1 test -f \"$2\" | tee -a "$oldrun/remote-log"
			return 1
		else
			echo " ---" Exit code $ret: ssh $1 test -f \"$2\", retry after $sleeptime seconds. `date` | tee -a "$oldrun/remote-log"
			return $ret
		fi
		sleep $sleeptime
	done
}

# Function to start batches on idle remote $systems
#
# Usage: startbatches curbatch nbatches
#
# Batches are numbered starting at 1.  Returns the next batch to start.
# Be careful to redirect all debug output to FD 2 (stderr).
startbatches () {
	local curbatch="$1"
	local nbatches="$2"
	local ret

	# Each pass through the following loop examines one system.
	for i in $systems
	do
		if test "$curbatch" -gt "$nbatches"
		then
			echo $((nbatches + 1))
			return 0
		fi
		if checkremotefile "$i" "$resdir/$ds/remote.run" 1>&2
		then
			continue # System still running last test, skip.
		fi
		ssh "$i" "cd \"$resdir/$ds\"; touch remote.run; PATH=\"$T/bin:$PATH\" nohup kvm-remote-$curbatch.sh > kvm-remote-$curbatch.sh.out 2>&1 &" 1>&2
		ret=$?
		if test "$ret" -ne 0
		then
			echo ssh $i failed: exitcode $ret 1>&2
			exit 11
		fi
		echo " ----" System $i Batch `head -n $curbatch < "$rundir"/scenarios | tail -1` `date` 1>&2
		curbatch=$((curbatch + 1))
	done
	echo $curbatch
}

# Launch all the scenarios.
nbatches="`wc -l "$rundir"/scenarios | awk '{ print $1 }'`"
curbatch=1
while test "$curbatch" -le "$nbatches"
do
	startbatches $curbatch $nbatches > $T/curbatch 2> $T/startbatches.stderr
	curbatch="`cat $T/curbatch`"
	if test -s "$T/startbatches.stderr"
	then
		cat "$T/startbatches.stderr" | tee -a "$oldrun/remote-log"
	fi
	if test "$curbatch" -le "$nbatches"
	then
		sleep 30
	fi
done
echo All batches started. `date` | tee -a "$oldrun/remote-log"

# Wait for all remaining scenarios to complete and collect results.
for i in $systems
do
	while checkremotefile "$i" "$resdir/$ds/remote.run"
	do
		sleep 30
	done
	echo " ---" Collecting results from $i `date` | tee -a "$oldrun/remote-log"
	( cd "$oldrun"; ssh $i "cd $rundir; tar -czf - kvm-remote-*.sh.out */console.log */kvm-test-1-run*.sh.out */qemu[_-]pid */qemu-retval */qemu-affinity; rm -rf $T > /dev/null 2>&1" | tar -xzf - )
done

( kvm-end-run-stats.sh "$oldrun" "$starttime"; echo $? > $T/exitcode ) | tee -a "$oldrun/remote-log"
exit "`cat $T/exitcode`"
