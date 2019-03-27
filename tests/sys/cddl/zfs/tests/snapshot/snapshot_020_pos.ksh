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
# Copyright 2014 Spectra Logic Corporation
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_020_pos
#
# DESCRIPTION:
#	Verify that snapshots can be mounted in the ctldir, then renamed, then
#	destroyed.  The original bug that this regresses was that the rename
#	command failed to unmount the snapshot from its old location, leaving
#	the user unable to either mount it or destroy it.
#
# STRATEGY:
#	1. Create snapshot
#	2. Access the snapshot through the ctldir to automount it
#	3. Rename the snapshot
#	4. Destroy the snapshot
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2014-06-02)
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

log_assert "Verify that mounted snapshots can be renamed and destroyed"
log_onexit cleanup

log_must $ZFS set snapdir=visible $TESTPOOL/$TESTFS
log_must $ZFS snapshot $SNAPFS
log_must stat $TESTDIR/$SNAPROOT/$TESTSNAP
log_must $ZFS rename $SNAPFS $TESTPOOL/$TESTFS@$TESTSNAP1
log_must stat $TESTDIR/$SNAPROOT/$TESTSNAP1
log_must $ZFS destroy $TESTPOOL/$TESTFS@$TESTSNAP1

log_pass
