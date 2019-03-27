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
# ident	"@(#)cleanup.ksh	1.3	08/11/03 SMI"
#

. $STF_SUITE/include/libtest.kshlib

verify_runnable "global"

ismounted $NONZFS_TESTDIR ufs
(( $? == 0 )) && log_must $UMOUNT -f $NONZFS_TESTDIR

ismounted $TESTPOOL/$TESTFS
[[ $? == 0 ]] && log_must $ZFS umount -f $TESTDIR
destroy_pool $TESTPOOL

# recreate and destroy a zpool over the disks to restore the partitions to
# normal
case $DISK_COUNT in
0)
	log_note "No disk devices to restore"
	;;
1)
	log_must cleanup_devices $ZFS_DISK
	;;
*)
	log_must cleanup_devices $ZFS_DISK $NONZFS_DISK
	;;
esac

log_pass
