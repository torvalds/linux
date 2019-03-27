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
# ident	"@(#)zfs_share_009_pos.ksh	1.2	08/11/03 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_share_009_pos
#
# DESCRIPTION:
# Verify that umount/rollback/destroy fails does not unshare the shared 
# file system
#
# STRATEGY:
# 1. Share the filesystem via 'zfs set sharenfs'.
# 2. Try umount failure, and verify that the file system is still shared.
# 3. Try rollback failure, and verify that the file system is still shared.
# 4. Try destroy failure, and verify that the file system is still shared.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-04-28)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	log_must cd $origdir

	log_must $ZFS set sharenfs=off $TESTPOOL/$TESTFS
	unshare_fs $TESTPOOL/$TESTFS

	if snapexists "$TESTPOOL/$TESTFS@snapshot"; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS@snapshot
	fi

	if datasetexists $TESTPOOL/$TESTFS/fs2 ; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS/fs2
	fi
}

log_assert "Verify umount/rollback/destroy fails does not unshare the shared" \
	"file system"
log_onexit cleanup

typeset origdir=$PWD

# unmount fails will not unshare the shared filesystem
log_must $ZFS set sharenfs=on $TESTPOOL/$TESTFS
log_must is_shared $TESTDIR
if cd $TESTDIR ; then
	log_mustnot $ZFS umount $TESTPOOL/$TESTFS
else
	log_fail "cd $TESTDIR fails"
fi
log_must is_shared $TESTDIR

# rollback fails will not unshare the shared filesystem
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snapshot
if cd $TESTDIR ; then
	log_mustnot $ZFS rollback $TESTPOOL/$TESTFS@snapshot
else
	log_fail "cd $TESTDIR fails"
fi
log_must is_shared $TESTDIR

# destroy fails will not unshare the shared filesystem
log_must $ZFS create $TESTPOOL/$TESTFS/fs2
if cd $TESTDIR/fs2 ; then
	log_mustnot $ZFS destroy $TESTPOOL/$TESTFS/fs2
else
	log_fail "cd $TESTDIR/fs2 fails"
fi
log_must is_shared $TESTDIR/fs2

log_pass "Verify umount/rollback/destroy fails does not unshare the shared" \
	"file system"
