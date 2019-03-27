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
# ident	"@(#)zfs_mount_008_pos.ksh	1.3	09/01/13 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_mount_008_pos
#
# DESCRIPTION:
#	'zfs mount -O' allow the file system to be mounted over an existing
#	mount point, making the underlying file system inaccessible.
#
# STRATEGY:
#	1. Create two filesystem fs & fs1, and create two test files for them.
#	2. Unmount fs1 and set mountpoint property is identical to fs.
#	3. Verify 'zfs mount -O' will make the underlying filesystem fs
#	   inaccessible.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-02)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	! ismounted $fs && log_must $ZFS mount $fs
	
	if datasetexists $fs1; then
		log_must $ZFS destroy $fs1
	fi

	if [[ -f $testfile ]]; then
		log_must $RM -f $testfile
	fi
}

log_assert "Verify 'zfs mount -O' will override existing mount point."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS; fs1=$TESTPOOL/$TESTFS1

cleanup

# Get the original mountpoint of $fs and $fs1
mntpnt=$(get_prop mountpoint $fs)
log_must $ZFS create $fs1
mntpnt1=$(get_prop mountpoint $fs1)

testfile=$mntpnt/$TESTFILE0; testfile1=$mntpnt1/$TESTFILE1
log_must $MKFILE 1M $testfile $testfile1

log_must $ZFS unmount $fs1
log_must $ZFS set mountpoint=$mntpnt $fs1
log_must $ZFS mount $fs1

# Create new file in override mountpoint
log_must $MKFILE 1M $mntpnt/$TESTFILE2

# Verify the underlying file system inaccessible
log_mustnot $LS $testfile
log_must $LS $mntpnt/$TESTFILE1 $mntpnt/$TESTFILE2

# Verify $TESTFILE2 was created in $fs1, rather then $fs
log_must $ZFS unmount $fs1
log_must $ZFS set mountpoint=$mntpnt1 $fs1
log_must $ZFS mount $fs1
log_must $LS $testfile1 $mntpnt1/$TESTFILE2

# Verify $TESTFILE2 was not created in $fs, and $fs is accessible again.
log_mustnot $LS $mntpnt/$TESTFILE2
log_must $LS $testfile

log_pass "Verify 'zfs mount -O' override mount point passed."
