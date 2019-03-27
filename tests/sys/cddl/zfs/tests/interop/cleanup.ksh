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
# ident	"@(#)cleanup.ksh	1.3	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib

verify_runnable "global"

ismounted $TESTPOOL/$TESTFS
(( $? == 0 )) && log_must $ZFS umount -f $TESTDIR
destroy_pool $TESTPOOL

$METASTAT $META_DEVICE_ID > /dev/null 2>&1
if (( $? == 0 )); then
	log_note "Clearing meta device ($META_DEVICE_ID)"
	$METACLEAR -f $META_DEVICE_ID > /dev/null 2>&1
fi

typeset metadb=""
typeset i=""

metadb=`$METADB | $CUT -f6 | $GREP dev | $UNIQ`
for i in $metadb; do
	log_note "Clearing meta db ($i)"
	$METADB -fd $i > /dev/null 2>&1
done

# recreate and destroy a zpool over the disks to restore the partitions to
# normal
case $DISK_COUNT in
0)
	log_note "No disk devices to restore"
	;;
1)
	log_must cleanup_devices $ZFS_DISK2
	;;
2)
	log_must cleanup_devices $META_DISK0 $ZFS_DISK2
	;;
*)
	log_must cleanup_devices $META_DISK0 $META_DISK1 $ZFS_DISK2
	;;
esac

log_pass "Cleanup has been successful"
