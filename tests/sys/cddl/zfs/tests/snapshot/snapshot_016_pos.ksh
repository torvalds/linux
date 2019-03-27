#!/usr/local/bin/ksh93 -p
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
# ident	"@(#)snapshot_016_pos.ksh	1.2	08/11/03 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_016_pos
#
# DESCRIPTION:
#	Verify renamed snapshots via mv can be destroyed
#
# STRATEGY:
#	1. Create snapshot
#	2. Rename the snapshot via mv command
#	2. Verify destroying the renamed snapshot via 'zfs destroy' succeeds
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-01-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	datasetexists $SNAPFS && \
		log_must $ZFS destroy -Rf $SNAPFS
	datasetexists $TESTPOOL/$TESTFS@snap_a && \
		log_must $ZFS destroy -Rf $TESTPOOL/$TESTFS@snap_a
	datasetexists $TESTPOOL/$TESTCLONE@snap_a && \
		log_must $ZFS destroy -Rf $TESTPOOL/$TESTCLONE@snap_a

	datasetexists $TESTPOOL/$TESTCLONE && \
		log_must $ZFS destroy $TESTPOOL/$TESTCLONE
	datasetexists $TESTPOOL/$TESTFS && \
		log_must $ZFS destroy $TESTPOOL/$TESTFS

	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Verify renamed snapshots via mv can be destroyed."
log_onexit cleanup

[[ $os_name == "FreeBSD" ]] && \
	log_uninitiated "Directory operations on the $(get_snapdir_name) directory are not yet supported in FreeBSD"

# scenario 1

log_must $ZFS snapshot $SNAPFS
log_must $MV $TESTDIR/$SNAPROOT/$TESTSNAP $TESTDIR/$SNAPROOT/snap_a

datasetexists $TESTPOOL/$TESTFS@snap_a || \
	log_fail "rename snapshot via mv in $(get_snapdir_name) fails."
log_must $ZFS destroy $TESTPOOL/$TESTFS@snap_a

# scenario 2

log_must $ZFS snapshot $SNAPFS
log_must $ZFS clone $SNAPFS $TESTPOOL/$TESTCLONE
log_must $MV $TESTDIR/$SNAPROOT/$TESTSNAP $TESTDIR/$SNAPROOT/snap_a

datasetexists $TESTPOOL/$TESTFS@snap_a || \
	log_fail "rename snapshot via mv in $(get_snapdir_name) fails."
log_must $ZFS promote $TESTPOOL/$TESTCLONE
# promote back to $TESTPOOL/$TESTFS for scenario 3
log_must $ZFS promote $TESTPOOL/$TESTFS
log_must $ZFS destroy $TESTPOOL/$TESTCLONE
log_must $ZFS destroy $TESTPOOL/$TESTFS@snap_a

# scenario 3

log_must $ZFS snapshot $SNAPFS
log_must $ZFS clone $SNAPFS $TESTPOOL/$TESTCLONE
log_must $ZFS rename $SNAPFS $TESTPOOL/$TESTFS@snap_a
log_must $ZFS promote $TESTPOOL/$TESTCLONE
log_must $ZFS destroy $TESTPOOL/$TESTFS
log_must $ZFS destroy $TESTPOOL/$TESTCLONE@snap_a

log_pass "Verify renamed snapshots via mv can be destroyed."
