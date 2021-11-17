#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Start up the specified number of jitter.sh scripts in the background.
#
# Usage: . jitterstart.sh n jittering-dir duration [ sleepmax [ spinmax ] ]
#
# n: Number of jitter.sh scripts to start up.
# jittering-dir: Directory in which to put "jittering" file.
# duration: Time to run in seconds.
# sleepmax: Maximum microseconds to sleep, defaults to one second.
# spinmax: Maximum microseconds to spin, defaults to one millisecond.
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

jitter_n=$1
if test -z "$jitter_n"
then
	echo jitterstart.sh: Missing count of jitter.sh scripts to start.
	exit 33
fi
jittering_dir=$2
if test -z "$jittering_dir"
then
	echo jitterstart.sh: Missing directory in which to place jittering file.
	exit 34
fi
shift
shift

touch ${jittering_dir}/jittering
for ((jitter_i = 1; jitter_i <= $jitter_n; jitter_i++))
do
	jitter.sh $jitter_i "${jittering_dir}/jittering" "$@" &
done
