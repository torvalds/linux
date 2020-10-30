#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Analyze a given results directory for rcutorture progress.
#
# Usage: kvm-recheck-rcu.sh resdir
#
# Copyright (C) Facebook, 2020
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

i="$1"
if test -d "$i" -a -r "$i"
then
	:
else
	echo Unreadable results directory: $i
	exit 1
fi
. functions.sh

configfile=`echo $i | sed -e 's/^.*\///'`
nscfs="`grep 'scf_invoked_count ver:' $i/console.log 2> /dev/null | tail -1 | sed -e 's/^.* scf_invoked_count ver: //' -e 's/ .*$//' | tr -d '\015'`"
if test -z "$nscfs"
then
	echo "$configfile ------- "
else
	dur="`sed -e 's/^.* scftorture.shutdown_secs=//' -e 's/ .*$//' < $i/qemu-cmd 2> /dev/null`"
	if test -z "$dur"
	then
		rate=""
	else
		nscfss=`awk -v nscfs=$nscfs -v dur=$dur '
			BEGIN { print nscfs / dur }' < /dev/null`
		rate=" ($nscfss/s)"
	fi
	echo "${configfile} ------- ${nscfs} SCF handler invocations$rate"
fi
