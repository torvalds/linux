#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Runs the C-language litmus tests having a maximum number of processes
# to run, defaults to 6.
#
# sh checkghlitmus.sh
#
# Run from the Linux kernel tools/memory-model directory.  See the
# parseargs.sh scripts for arguments.

. scripts/parseargs.sh
. scripts/hwfnseg.sh

T=/tmp/checkghlitmus.sh.$$
trap 'rm -rf $T' 0
mkdir $T

# Clone the repository if it is not already present.
if test -d litmus
then
	:
else
	git clone https://github.com/paulmckrcu/litmus
	( cd litmus; git checkout origin/master )
fi

# Create any new directories that have appeared in the github litmus
# repo since the last run.
if test "$LKMM_DESTDIR" != "."
then
	find litmus -type d -print |
	( cd "$LKMM_DESTDIR"; sed -e 's/^/mkdir -p /' | sh )
fi

# Create a list of the specified litmus tests previously run.
( cd $LKMM_DESTDIR; find litmus -name "*.litmus${hwfnseg}.out" -print ) |
	sed -e "s/${hwfnseg}"'\.out$//' |
	xargs -r egrep -l '^ \* Result: (Never|Sometimes|Always|DEADLOCK)' |
	xargs -r grep -L "^P${LKMM_PROCS}"> $T/list-C-already

# Create a list of C-language litmus tests with "Result:" commands and
# no more than the specified number of processes.
find litmus -name '*.litmus' -print | mselect7 -arch C > $T/list-C
xargs < $T/list-C -r egrep -l '^ \* Result: (Never|Sometimes|Always|DEADLOCK)' > $T/list-C-result
xargs < $T/list-C-result -r grep -L "^P${LKMM_PROCS}" > $T/list-C-result-short

# Form list of tests without corresponding .out files
sort $T/list-C-already $T/list-C-result-short | uniq -u > $T/list-C-needed

# Run any needed tests.
if scripts/runlitmushist.sh < $T/list-C-needed > $T/run.stdout 2> $T/run.stderr
then
	errs=
else
	errs=1
fi

sed < $T/list-C-result-short -e 's,^,scripts/judgelitmus.sh ,' |
	sh > $T/judge.stdout 2> $T/judge.stderr

if test -n "$errs"
then
	cat $T/run.stderr 1>&2
fi
grep '!!!' $T/judge.stdout
