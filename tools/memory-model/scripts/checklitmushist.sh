#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Reruns the C-language litmus tests previously run that match the
# specified criteria, and compares the result to that of the previous
# runs from initlitmushist.sh and/or newlitmushist.sh.
#
# sh checklitmushist.sh
#
# Run from the Linux kernel tools/memory-model directory.
# See scripts/parseargs.sh for list of arguments.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

. scripts/parseargs.sh

T=/tmp/checklitmushist.sh.$$
trap 'rm -rf $T' 0
mkdir $T

if test -d litmus
then
	:
else
	echo Run scripts/initlitmushist.sh first, need litmus repo.
	exit 1
fi

# Create the results directory and populate it with subdirectories.
# The initial output is created here to avoid clobbering the output
# generated earlier.
mkdir $T/results
find litmus -type d -print | ( cd $T/results; sed -e 's/^/mkdir -p /' | sh )

# Create the list of litmus tests already run, then remove those that
# are excluded by this run's --procs argument.
( cd $LKMM_DESTDIR; find litmus -name '*.litmus.out' -print ) |
	sed -e 's/\.out$//' |
	xargs -r grep -L "^P${LKMM_PROCS}"> $T/list-C-already
xargs < $T/list-C-already -r grep -L "^P${LKMM_PROCS}" > $T/list-C-short

# Redirect output, run tests, then restore destination directory.
destdir="$LKMM_DESTDIR"
LKMM_DESTDIR=$T/results; export LKMM_DESTDIR
scripts/runlitmushist.sh < $T/list-C-short > $T/runlitmushist.sh.out 2>&1
LKMM_DESTDIR="$destdir"; export LKMM_DESTDIR

# Move the newly generated .litmus.out files to .litmus.out.new files
# in the destination directory.
cdir=`pwd`
ddir=`awk -v c="$cdir" -v d="$LKMM_DESTDIR" \
	'END { if (d ~ /^\//) print d; else print c "/" d; }' < /dev/null`
( cd $T/results; find litmus -type f -name '*.litmus.out' -print |
  sed -e 's,^.*$,cp & '"$ddir"'/&.new,' | sh )

sed < $T/list-C-short -e 's,^,'"$LKMM_DESTDIR/"',' |
	sh scripts/cmplitmushist.sh
exit $?
