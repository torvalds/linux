#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Runs the C-language litmus tests matching the specified criteria
# that do not already have a corresponding .litmus.out file, and does
# not judge the result.
#
# sh newlitmushist.sh
#
# Run from the Linux kernel tools/memory-model directory.
# See scripts/parseargs.sh for list of arguments.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

. scripts/parseargs.sh

T=/tmp/newlitmushist.sh.$$
trap 'rm -rf $T' 0
mkdir $T

if test -d litmus
then
	:
else
	echo Run scripts/initlitmushist.sh first, need litmus repo.
	exit 1
fi

# Create any new directories that have appeared in the github litmus
# repo since the last run.
if test "$LKMM_DESTDIR" != "."
then
	find litmus -type d -print |
	( cd "$LKMM_DESTDIR"; sed -e 's/^/mkdir -p /' | sh )
fi

# Create a list of the C-language litmus tests previously run.
( cd $LKMM_DESTDIR; find litmus -name '*.litmus.out' -print ) |
	sed -e 's/\.out$//' |
	xargs -r grep -L "^P${LKMM_PROCS}"> $T/list-C-already

# Form full list of litmus tests with no more than the specified
# number of processes (per the --procs argument).
find litmus -name '*.litmus' -exec grep -l -m 1 "^C " {} \; > $T/list-C-all
xargs < $T/list-C-all -r grep -L "^P${LKMM_PROCS}" > $T/list-C-short

# Form list of new tests.  Note: This does not handle litmus-test deletion!
sort $T/list-C-already $T/list-C-short | uniq -u > $T/list-C-new

# Form list of litmus tests that have changed since the last run.
sed < $T/list-C-short -e 's,^.*$,if test & -nt '"$LKMM_DESTDIR"'/&.out; then echo &; fi,' > $T/list-C-script
sh $T/list-C-script > $T/list-C-newer

# Merge the list of new and of updated litmus tests: These must be (re)run.
sort -u $T/list-C-new $T/list-C-newer > $T/list-C-needed

scripts/runlitmushist.sh < $T/list-C-needed

exit 0
