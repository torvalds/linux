#! /usr/local/bin/ksh93 -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD$

#
# Copyright 2013 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_019_pos
#
# DESCRIPTION:
#	Accessing snapshots and unmounting them in parallel does not panic.
#	FreeBSD PR kern/184677
#
# STRATEGY:
#	1. Create a dataset
#	2. Set the snapdir property to visible
#	3. Do the following in parallel
#	   a. Repeatedly access the snapshot
#	   b. Repeatedly unmount the snapshot
#	4. Verify that the system does not panic
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2013-12-23)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

KILL_SWITCH=${PWD}/kill_switch

function stat_snapshot
{
	while [ ! -f ${KILL_SWITCH} ]; do
		cd $SNAPDIR # Exercise forced unmount
		stat "$SNAPDIR" > /dev/null 2>&1
	done
}

function ls_snapshot
{
	# Pre-generate the argument list.
	ls_args=""
	for ((num=0; $num<100; num=$num+1)); do
		ls_args="$ls_args $SNAPDIR/.."
	done

	while [ ! -f ${KILL_SWITCH} ]; do
		ls $ls_args >/dev/null 2>&1
	done
}

log_assert "Accessing snapshots and unmounting them in parallel does not panic"

log_must dataset_setprop $TESTPOOL "snapdir" "visible"

# Take snapshots
log_must $ZFS snapshot "$TESTPOOL/$TESTFS@$TESTSNAP"

# Repeatedly access the snapshot directory
stat_snapshot &
stat_pid="$!"
ls_snapshot &
ls_pid="$!"

# Repeatedly unmount the snapshot directory
for ((i=0; $i<100; i=$i+1)); do
	umount "$SNAPDIR" >/dev/null 2>&1
	log_note "$i non-forced done"
	# Sleep just long enough for the other "threads" to remount.
	sleep 0.1
	umount -f "$SNAPDIR" >/dev/null 2>&1
	log_note "$i forced done"
	sleep 0.1
done

# Kill the other "threads" and wait for them to die.
touch $KILL_SWITCH
log_note "Waiting for all child processes to die..."
wait

# Test that no reference leaks occurred and we can cleanup without forcing.
log_must $ZFS unmount $TESTPOOL/$TESTFS
log_must $ZFS destroy -r $TESTPOOL/$TESTFS

# If we get here, we managed to not panic, deadlock, or leak references.
log_pass
