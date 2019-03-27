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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_share_004_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_share_004_pos
#
# DESCRIPTION:
# Verify that a file system and its snapshot are shared.
#
# STRATEGY:
# 1. Create a file system
# 2. Set the sharenfs property on the file system
# 3. Create a snapshot
# 4. Verify that both are shared.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if snapexists $TESTPOOL/$TESTFS@snapshot; then
		log_must $ZFS destroy $TESTPOOL/$TESTFS@snapshot
	fi

	log_must $ZFS set sharenfs=off $TESTPOOL/$TESTFS
	log_must unshare_fs $TESTPOOL/$TESTFS
}

#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# share the mountpoint and then verify a snapshot of the mounpoint
# is also shared.
#
function test_snap_share # mntp filesystem
{
        typeset mntp=$1
        typeset filesystem=$2

        not_shared $mntp || \
            log_fail "File system $filesystem is already shared."

        log_must $ZFS set sharenfs=on $filesystem
        is_shared $mntp || \
            log_fail "File system $filesystem is not shared (set sharenfs)."

	log_must $LS -l  $mntp/$SNAPROOT/snapshot
        #
        # Verify 'zfs share' works as well.
        #
        log_must $ZFS unshare $filesystem
        log_must $ZFS share $filesystem

        is_shared $mntp || \
            log_fail "file system $filesystem is not shared (zfs share)."

	log_must $LS -l  $mntp/$SNAPROOT/snapshot
}

log_assert "Verify that a file system and its snapshot are shared."
log_onexit cleanup

log_must $ZFS snapshot $TESTPOOL/$TESTFS@snapshot
test_snap_share $TESTDIR $TESTPOOL/$TESTFS

log_pass "A file system and its snapshot are both shared as expected."
