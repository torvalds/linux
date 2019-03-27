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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)userquota_009_pos.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_009_pos
#
# DESCRIPTION:
#       Check user|group quota to snapshot that:
#	1) can not set user|group quota to snapshot directly
#	2) snapshot can inherit the parent fs's user|groupquota
#	3) the user|group quota will not change even the parent fs's quota changed.
#
#
# STRATEGY:
#       1. create a snapshot of a fs
#       2. set the user|group quota to snapshot and expect fail
#	3. set user|group quota to fs and check the snapshot
#	4. re-set user|group quota to fs and check the snapshot's value
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
###############################################################################

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

function cleanup
{
	if datasetexists $snap_fs; then
		log_must $ZFS destroy $snap_fs
	fi

	log_must cleanup_quota	
}

log_onexit cleanup

log_assert "Check the snapshot's user|group quota"
typeset snap_fs=$QFS@snap


log_must $ZFS set userquota@$QUSER1=$UQUOTA_SIZE $QFS
log_must check_quota "userquota@$QUSER1" $QFS "$UQUOTA_SIZE"

log_must $ZFS set groupquota@$QGROUP=$GQUOTA_SIZE $QFS
log_must check_quota "groupquota@$QGROUP" $QFS "$GQUOTA_SIZE"

log_must $ZFS snapshot $snap_fs

log_note "check the snapshot $snap_fs user|group quota"
log_must check_quota "userquota@$QUSER1" $snap_fs "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $snap_fs "$GQUOTA_SIZE"

log_note  "set userquota and groupquota to $snap_fs which will fail"
log_mustnot $ZFS set userquota@$QUSER1=$SNAP_QUOTA $snap_fs
log_mustnot $ZFS set groupquota@$QGROUP=$SNAP_QUOTA $snap_fs

log_note "change the parent's userquota and groupquota"
log_must $ZFS set userquota@$QUSER1=$TEST_QUOTA $QFS
log_must $ZFS set groupquota@$QGROUP=$TEST_QUOTA $QFS

log_must check_quota "userquota@$QUSER1" $QFS $TEST_QUOTA
log_must check_quota "groupquota@$QGROUP" $QFS $TEST_QUOTA

log_note "check the snapshot $snap_fs userquota and groupquota"
log_must check_quota "userquota@$QUSER1" $snap_fs "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $snap_fs "$GQUOTA_SIZE"

log_pass "Check the snapshot's user|group quota pass as expect"
