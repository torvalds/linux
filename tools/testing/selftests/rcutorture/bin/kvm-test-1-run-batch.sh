#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Carry out a kvm-based run for the specified batch of scenarios, which
# might have been built by --build-only kvm.sh run.
#
# Usage: kvm-test-1-run-batch.sh SCENARIO [ SCENARIO ... ]
#
# Each SCENARIO is the name of a directory in the current directory
#	containing a ready-to-run qemu-cmd file.
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

T=${TMPDIR-/tmp}/kvm-test-1-run-batch.sh.$$
trap 'rm -rf $T' 0
mkdir $T

echo ---- Running batch $*
# Check arguments
runfiles=
for i in "$@"
do
	if ! echo $i | grep -q '^[^/.a-z]\+\(\.[0-9]\+\)\?$'
	then
		echo Bad scenario name: \"$i\" 1>&2
		exit 1
	fi
	if ! test -d "$i"
	then
		echo Scenario name not a directory: \"$i\" 1>&2
		exit 2
	fi
	if ! test -f "$i/qemu-cmd"
	then
		echo Scenario lacks a command file: \"$i/qemu-cmd\" 1>&2
		exit 3
	fi
	rm -f $i/build.*
	touch $i/build.run
	runfiles="$runfiles $i/build.run"
done

# Extract settings from the qemu-cmd file.
grep '^#' $1/qemu-cmd | sed -e 's/^# //' > $T/qemu-cmd-settings
. $T/qemu-cmd-settings

# Start up jitter, start each scenario, wait, end jitter.
echo ---- System running test: `uname -a`
echo ---- Starting kernels. `date` | tee -a log
$TORTURE_JITTER_START
for i in "$@"
do
	echo ---- System running test: `uname -a` > $i/kvm-test-1-run-qemu.sh.out
	echo > $i/kvm-test-1-run-qemu.sh.out
	kvm-test-1-run-qemu.sh $i >> $i/kvm-test-1-run-qemu.sh.out 2>&1 &
done
for i in $runfiles
do
	while ls $i > /dev/null 2>&1
	do
		:
	done
done
echo ---- All kernel runs complete. `date` | tee -a log
$TORTURE_JITTER_STOP
