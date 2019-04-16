#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Check the build output from an rcutorture run for goodness.
# The "file" is a pathname on the local system, and "title" is
# a text string for error-message purposes.
#
# The file must contain kernel build output.
#
# Usage: parse-build.sh file title
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

F=$1
title=$2
T=${TMPDIR-/tmp}/parse-build.sh.$$
trap 'rm -rf $T' 0
mkdir $T

. functions.sh

if grep -q CC < $F
then
	:
else
	print_bug $title no build
	exit 1
fi

if grep -q "error:" < $F
then
	print_bug $title build errors:
	grep "error:" < $F
	exit 2
fi

grep warning: < $F > $T/warnings
grep "include/linux/*rcu*\.h:" $T/warnings > $T/hwarnings
grep "kernel/rcu/[^/]*:" $T/warnings > $T/cwarnings
cat $T/hwarnings $T/cwarnings > $T/rcuwarnings
if test -s $T/rcuwarnings
then
	print_warning $title build errors:
	cat $T/rcuwarnings
	exit 2
fi
exit 0
