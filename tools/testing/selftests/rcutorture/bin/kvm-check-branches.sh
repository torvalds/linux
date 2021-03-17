#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Run a group of kvm.sh tests on the specified commits.  This currently
# unconditionally does three-minute runs on each scenario in CFLIST,
# taking advantage of all available CPUs and trusting the "make" utility.
# In the short term, adjustments can be made by editing this script and
# CFLIST.  If some adjustments appear to have ongoing value, this script
# might grow some command-line arguments.
#
# Usage: kvm-check-branches.sh commit1 commit2..commit3 commit4 ...
#
# This script considers its arguments one at a time.  If more elaborate
# specification of commits is needed, please use "git rev-list" to
# produce something that this simple script can understand.  The reason
# for retaining the simplicity is that it allows the user to more easily
# see which commit came from which branch.
#
# This script creates a yyyy.mm.dd-hh.mm.ss-group entry in the "res"
# directory.  The calls to kvm.sh create the usual entries, but this script
# moves them under the yyyy.mm.dd-hh.mm.ss-group entry, each in its own
# directory numbered in run order, that is, "0001", "0002", and so on.
# For successful runs, the large build artifacts are removed.  Doing this
# reduces the disk space required by about two orders of magnitude for
# successful runs.
#
# Copyright (C) Facebook, 2020
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

if ! git status > /dev/null 2>&1
then
	echo '!!!' This script needs to run in a git archive. 1>&2
	echo '!!!' Giving up. 1>&2
	exit 1
fi

# Remember where we started so that we can get back and the end.
curcommit="`git status | head -1 | awk '{ print $NF }'`"

nfail=0
ntry=0
resdir="tools/testing/selftests/rcutorture/res"
ds="`date +%Y.%m.%d-%H.%M.%S`-group"
if ! test -e $resdir
then
	mkdir $resdir || :
fi
mkdir $resdir/$ds
echo Results directory: $resdir/$ds

KVM="`pwd`/tools/testing/selftests/rcutorture"; export KVM
PATH=${KVM}/bin:$PATH; export PATH
. functions.sh
cpus="`identify_qemu_vcpus`"
echo Using up to $cpus CPUs.

# Each pass through this loop does one command-line argument.
for gitbr in $@
do
	echo ' --- git branch ' $gitbr

	# Each pass through this loop tests one commit.
	for i in `git rev-list "$gitbr"`
	do
		ntry=`expr $ntry + 1`
		idir=`awk -v ntry="$ntry" 'END { printf "%04d", ntry; }' < /dev/null`
		echo ' --- commit ' $i from branch $gitbr
		date
		mkdir $resdir/$ds/$idir
		echo $gitbr > $resdir/$ds/$idir/gitbr
		echo $i >> $resdir/$ds/$idir/gitbr

		# Test the specified commit.
		git checkout $i > $resdir/$ds/$idir/git-checkout.out 2>&1
		echo git checkout return code: $? "(Commit $ntry: $i)"
		kvm.sh --cpus $cpus --duration 3 --trust-make > $resdir/$ds/$idir/kvm.sh.out 2>&1
		ret=$?
		echo kvm.sh return code $ret for commit $i from branch $gitbr

		# Move the build products to their resting place.
		runresdir="`grep -m 1 '^Results directory:' < $resdir/$ds/$idir/kvm.sh.out | sed -e 's/^Results directory://'`"
		mv $runresdir $resdir/$ds/$idir
		rrd="`echo $runresdir | sed -e 's,^.*/,,'`"
		echo Run results: $resdir/$ds/$idir/$rrd
		if test "$ret" -ne 0
		then
			# Failure, so leave all evidence intact.
			nfail=`expr $nfail + 1`
		else
			# Success, so remove large files to save about 1GB.
			( cd $resdir/$ds/$idir/$rrd; rm -f */vmlinux */bzImage */System.map */Module.symvers )
		fi
	done
done
date

# Go back to the original commit.
git checkout "$curcommit"

if test $nfail -ne 0
then
	echo '!!! ' $nfail failures in $ntry 'runs!!!'
	exit 1
else
	echo No failures in $ntry runs.
	exit 0
fi
