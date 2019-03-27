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
# ident	"@(#)zpool_create_004_pos.ksh	1.4	08/11/03 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_004_pos
#
# DESCRIPTION:
# 'zpool create [-f]' can create a storage pool with large number of
# file-in-zfs-filesystem-based vdevs without any errors.
#
# STRATEGY:
# 1. Create assigned number of files in ZFS filesystem as vdevs
# 2. Creating a new pool based on the vdevs should get success
# 3. Fill in the filesystem and create a partially writen file as vdev
# 4. Add the new file into vdevs list and create a pool
# 5. Creating a storage pool with the new vdevs list should be failed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-08-25)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

VDEVS_POOL=create_004_pool

function cleanup
{
	destroy_pool $VDEVS_POOL
	destroy_pool $TESTPOOL2
	destroy_pool $TESTPOOL1
	destroy_pool $TESTPOOL
	[ -d "$TESTDIR" ] && log_must $RM -rf $TESTDIR
}

log_assert "'zpool create [-f]' can create a pool with $VDEVS_NUM vdevs " \
		"without any errors."
log_onexit cleanup

log_note "Creating storage pool with $VDEVS_NUM file vdevs should succeed."
vdevs_list=""
file_size=$FILE_SIZE

[ -n "$DISK" ] && disk=$DISK || disk=$DISK0
create_pool $TESTPOOL $disk
$ZFS create -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS

for (( i = 0; $i < $VDEVS_NUM; i++ )); do
	log_must create_vdevs $TESTDIR/vdev.${i}
	vdevs_list="$vdevs_list $TESTDIR/vdev.${i}"
done

create_pool $TESTPOOL1 $vdevs_list
destroy_pool $TESTPOOL1

log_pass "'zpool create [-f]' with $VDEVS_NUM vdevs success."
