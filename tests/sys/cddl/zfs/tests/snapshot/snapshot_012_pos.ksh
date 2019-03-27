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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)snapshot_012_pos.ksh	1.3	08/05/14 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_012_pos
#
# DESCRIPTION:
#	Verify 'snapshot -r' can create snapshot for promoted clone, and vice
#	versa, a clone filesystem from the snapshot created by 'snapshot -r' 
#	can be correctly promoted.
#
# STRATEGY:
#	1. Create a dataset tree
#	2. snapshot a filesystem and clone the snapshot
#	3. promote the clone
#	4. snapshot -r the dataset tree
#	5. verify that the snapshot of cloned filesystem is created correctly
#	6. clone a snapshot from the snapshot tree
#	7. promote the clone
#	8. verify that the clone is promoted correctly.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if datasetexists $clone1; then
		log_must $ZFS promote $ctrfs
		log_must $ZFS destroy $clone1
	fi

	snapexists $snapctr && \
		log_must $ZFS destroy -r $snapctr

	if snapexists $clone@$TESTSNAP1; then
		log_must $ZFS promote $ctrfs
		log_must $ZFS destroy -rR $ctrfs@$TESTSNAP1
	fi
}

log_assert "Verify that 'snapshot -r' can work with 'zfs promote'."
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS1
clone=$ctr/$TESTCLONE
clone1=$ctr/$TESTCLONE1
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapctrclone=$clone@$TESTSNAP
snapctrclone1=$clone1@$TESTSNAP
snapctrfs=$SNAPCTR

#preparation for testing
log_must $ZFS snapshot $ctrfs@$TESTSNAP1
log_must $ZFS clone $ctrfs@$TESTSNAP1 $clone
log_must $ZFS promote $clone

log_must $ZFS snapshot -r $snapctr

! snapexists $snapctrclone && \
	log_fail "'snapshot -r' fails to create $snapctrclone for $ctr/$TESTCLONE."

log_must $ZFS clone $snapctrfs $clone1
log_must $ZFS promote $clone1

#verify the origin value is correct.
orig_value=$(get_prop origin $ctrfs)
if ! snapexists $snapctrclone1 || [[ "$orig_value" != "$snapctrclone1" ]]; then
	log_fail "'zfs promote' fails to promote $clone which is cloned from \
		$snapctrfs."
fi

log_pass "'snapshot -r' can work with 'zfs promote' as expected."
