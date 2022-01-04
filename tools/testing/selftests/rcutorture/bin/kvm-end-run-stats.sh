#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Check the status of the specified run.
#
# Usage: kvm-end-run-stats.sh /path/to/run starttime
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

# scriptname=$0
# args="$*"
rundir="$1"
if ! test -d "$rundir"
then
	echo kvm-end-run-stats.sh: Specified run directory does not exist: $rundir
	exit 1
fi

T=${TMPDIR-/tmp}/kvm-end-run-stats.sh.$$
trap 'rm -rf $T' 0
mkdir $T

KVM="`pwd`/tools/testing/selftests/rcutorture"; export KVM
PATH=${KVM}/bin:$PATH; export PATH
. functions.sh
default_starttime="`get_starttime`"
starttime="${2-default_starttime}"

echo | tee -a "$rundir/log"
echo | tee -a "$rundir/log"
echo " --- `date` Test summary:" | tee -a "$rundir/log"
echo Results directory: $rundir | tee -a "$rundir/log"
kcsan-collapse.sh "$rundir" | tee -a "$rundir/log"
kvm-recheck.sh "$rundir" > $T/kvm-recheck.sh.out 2>&1
ret=$?
cat $T/kvm-recheck.sh.out | tee -a "$rundir/log"
echo " --- Done at `date` (`get_starttime_duration $starttime`) exitcode $ret" | tee -a "$rundir/log"
exit $ret
