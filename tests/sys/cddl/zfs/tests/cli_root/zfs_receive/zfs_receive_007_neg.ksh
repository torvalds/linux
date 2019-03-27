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
# ident	"@(#)zfs_receive_007_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_007_neg
#
# DESCRIPTION:
#	'zfs recv -F' should fail if the incremental stream does not match
#
# STRATEGY:
#	1. Create pool and fs.
#	2. Create some files in fs and take snapshots.
#	3. Keep the incremental stream and restore the stream to the pool
#	4. Verify receiving the stream fails
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-10-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	for snap in $snap2 $snap1; do
		datasetexists $snap && log_must $ZFS destroy -rf $snap
	done
	for file in $ibackup $mntpnt/file1 $mntpnt/file2; do
		[[ -f $file ]] && log_must $RM -f $file
	done
}

log_assert "'zfs recv -F' should fail if the incremental stream does not match"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
snap1=$fs@snap1
snap2=$fs@snap2
ibackup=$TMPDIR/ibackup.${TESTCASE_ID}

datasetexists $fs || log_must $ZFS create $fs

mntpnt=$(get_prop mountpoint $fs) || log_fail "get_prop mountpoint $fs"
log_must $MKFILE 10m $mntpnt/file1
log_must $ZFS snapshot $snap1
log_must $MKFILE 10m $mntpnt/file2
log_must $ZFS snapshot $snap2

log_must eval "$ZFS send -i $snap1 $snap2 > $ibackup"

log_must $ZFS destroy $snap1
log_must $ZFS destroy $snap2
log_mustnot eval "$ZFS receive -F $fs < $ibackup"

log_must $MKFILE 20m $mntpnt/file1
log_must $RM -rf $mntpnt/file2
log_must $ZFS snapshot $snap1
log_mustnot eval "$ZFS receive -F $snap2 < $ibackup"

log_pass "'zfs recv -F' should fail if the incremental stream does not match"
