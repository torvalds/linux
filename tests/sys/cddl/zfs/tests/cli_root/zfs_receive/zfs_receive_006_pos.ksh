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
# ident	"@(#)zfs_receive_006_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_006_pos
#
# DESCRIPTION:
#	'zfs recv -d <fs>' should create ancestor filesystem if it does not
#   exist and it should not fail if it exists
#
# STRATEGY:
#	1. Create pool and fs.
#	2. Create some files in fs and take snapshots.
#	3. Keep the stream and restore the stream to the pool
#	4. Verify receiving the stream succeeds, and the ancestor filesystem 
#	   is created if it did not exist
#	5. Verify receiving the stream still succeeds when ancestor filesystem
#	   exists
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
	for file in $fbackup1 $fbackup2 $mntpnt/file1 $mntpnt/file2; do
		[[ -f $file ]] && log_must $RM -f $file
	done

	if is_global_zone; then
		datasetexists $TESTPOOL/$TESTFS/$TESTFS1 && \
			log_must $ZFS destroy -rf $TESTPOOL/$TESTFS/$TESTFS1
	else
		datasetexists $TESTPOOL/${ZONE_CTR}0 && \
			log_must $ZFS destroy -rf $TESTPOOL/${ZONE_CTR}0
	fi
	
}

log_assert "'zfs recv -d <fs>' should succeed no matter ancestor filesystem \
	exists."
log_onexit cleanup

ancestor_fs=$TESTPOOL/$TESTFS
fs=$TESTPOOL/$TESTFS/$TESTFS1
snap1=$fs@snap1
snap2=$fs@snap2
fbackup1=$TMPDIR/fbackup1.${TESTCASE_ID}
fbackup2=$TMPDIR/fbackup2.${TESTCASE_ID}

datasetexists $ancestor_fs || \
	log_must $ZFS create $ancestor_fs
log_must $ZFS create $fs

mntpnt=$(get_prop mountpoint $fs) || log_fail "get_prop mountpoint $fs"
log_must $MKFILE 10m $mntpnt/file1
log_must $ZFS snapshot $snap1
log_must $MKFILE 10m $mntpnt/file2
log_must $ZFS snapshot $snap2

log_must eval "$ZFS send $snap1 > $fbackup1"
log_must eval "$ZFS send $snap2 > $fbackup2"

log_note "Verify 'zfs receive -d' succeed and create ancestor filesystem \
	 if it did not exist. "
log_must $ZFS destroy -rf $ancestor_fs
log_must eval "$ZFS receive -d $TESTPOOL < $fbackup1"
is_global_zone || ancestor_fs=$TESTPOOL/${ZONE_CTR}0/$TESTFS
datasetexists $ancestor_fs || \
	log_fail "ancestor filesystem is not created"

log_note "Verify 'zfs receive -d' still succeed if ancestor filesystem exists"
is_global_zone || fs=$TESTPOOL/${ZONE_CTR}0/$TESTFS/$TESTFS1
log_must $ZFS destroy -rf $fs
log_must eval "$ZFS receive -d $TESTPOOL < $fbackup2"

log_pass "'zfs recv -d <fs>' should succeed no matter ancestor filesystem \
	exists."
