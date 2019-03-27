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
# ident	"@(#)zfs_receive_003_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_003_pos
#
# DESCRIPTION:
#	'zfs recv -F' to force rollback. 
#
# STRATEGY:
#	1. Create pool and fs.
#	2. Create some files in fs and take a snapshot1.
#	3. Create another files in fs and take snapshot2.
#	4. Create incremental stream from snapshot1 to snapshot2.
#	5. fs rollback to snapshot1 and modify fs.
#	6. Verify 'zfs recv -F' can force rollback.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-18)
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

log_assert "'zfs recv -F' to force rollback."
log_onexit cleanup

ibackup=$TMPDIR/ibackup.${TESTCASE_ID}
fs=$TESTPOOL/$TESTFS; snap1=$fs@snap1; snap2=$fs@snap2

mntpnt=$(get_prop mountpoint $fs) || log_fail "get_prop mountpoint $fs"
log_must $MKFILE 10m $mntpnt/file1
log_must $ZFS snapshot $snap1
log_must $MKFILE 10m $mntpnt/file2
log_must $ZFS snapshot $snap2

log_must eval "$ZFS send -i $snap1 $snap2 > $ibackup"

log_note "Verify 'zfs receive' succeed, if filesystem was not modified."
log_must $ZFS rollback -r $snap1
log_must eval "$ZFS receive $fs < $ibackup"
if [[ ! -f $mntpnt/file1 || ! -f $mntpnt/file2 ]]; then
	log_fail "'$ZFS receive' failed."
fi

log_note "Verify 'zfs receive' failed if filesystem was modified."
log_must $ZFS rollback -r $snap1
log_must $RM -rf $mntpnt/file1
log_mustnot eval "$ZFS receive $fs < $ibackup"

# Verify 'zfs receive -F' to force rollback whatever filesystem was modified.
log_must $ZFS rollback -r $snap1
log_must $RM -rf $mntpnt/file1
log_must eval "$ZFS receive -F $fs < $ibackup"
if [[ ! -f $mntpnt/file1 || ! -f $mntpnt/file2 ]]; then
	log_fail "'$ZFS receive -F' failed."
fi

log_pass "'zfs recv -F' to force rollback passed."
