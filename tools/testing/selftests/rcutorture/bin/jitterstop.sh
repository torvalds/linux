#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Remove the "jittering" file, signaling the jitter.sh scripts to stop,
# then wait for them to terminate.
#
# Usage: . jitterstop.sh jittering-dir
#
# jittering-dir: Directory containing "jittering" file.
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

jittering_dir=$1
if test -z "$jittering_dir"
then
	echo jitterstop.sh: Missing directory in which to place jittering file.
	exit 34
fi

rm -f ${jittering_dir}/jittering
wait
