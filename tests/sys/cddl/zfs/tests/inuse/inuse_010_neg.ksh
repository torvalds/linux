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

#
# Copyright 2017 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#
# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
set_disks

FSSIZE=1024	#reduce filesystem size, just to speed up newfs
MOUNTPOINT=$TMPDIR/inuse_010_neg_mp

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	$UMOUNT $MOUNTPOINT
	cleanup_devices $DISK0
	$RMDIR $MOUNTPOINT
}

log_onexit cleanup

log_assert "ZFS shouldn't be able to use a disk with a mounted filesystem"

log_must $NEWFS -s $FSSIZE $DISK0
log_must $MKDIR $MOUNTPOINT
log_must $MOUNT $DISK0 $MOUNTPOINT
log_mustnot $ZPOOL create $TESTPOOL $DISK0

log_pass "ZFS cannot use a disk with a mounted filesystem"
