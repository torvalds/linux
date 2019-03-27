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
# ident	"@(#)setup.ksh	1.5	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib

verify_runnable "global"

case $DISK_COUNT in
0)
	log_untested "Need at least 1 disk device for test"
	;;
1)
	log_note "Partitioning a single disk ($SINGLE_DISK)"
	;;
2)
	log_note "Partitioning a disks ($SINGLE_DISK) and ($ZFS_DISK2)"
	;;
3)
	log_note "Partitioning disks ($META_DISK0 $META_DISK1 $ZFS_DISK2)"
	;;
esac

wipe_partition_table $META_DISK0
log_must set_partition ${META_SIDE0##*s} "" $FS_SIZE $META_DISK0
if [[ $WRAPPER == *"smi"* && $META_DISK1 == $META_DISK0 ]]; then
	typeset i=${META_SIDE0##*s}
	typeset cyl=$(get_endslice $META_DISK0 $i)
	log_must set_partition ${META_SIDE1##*s} "$cyl" $FS_SIZE $META_DISK1
else
	log_must set_partition ${META_SIDE1##*s} "" $FS_SIZE $META_DISK1
fi
if [[ $WRAPPER == *"smi"* && $ZFS_DISK2 == $META_DISK1 ]]; then
	typeset i=${META_SIDE1##*s}
	typeset cyl=$(get_endslice $META_DISK1 $i)
	log_must set_partition ${ZFS_SIDE2##*s} "$cyl" $FS_SIZE $ZFS_DISK2
else
	log_must set_partition ${ZFS_SIDE2##*s} "" $FS_SIZE $ZFS_DISK2
fi

create_pool $TESTPOOL $ZFS_SIDE2
                        
$RM -rf $TESTDIR  || log_unresolved Could not remove $TESTDIR
$MKDIR -p $TESTDIR || log_unresolved Could not create $TESTDIR

log_must $ZFS create $TESTPOOL/$TESTFS
log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must $ZFS set compression=off $TESTPOOL/$TESTFS

log_note "Configuring metadb with $META_SIDE1"
log_must $METADB -a -f -c 3 $META_SIDE1

log_note "Configure $META_DEVICE_ID with $META_SIDE0"
log_must $METAINIT $META_DEVICE_ID 1 1 $META_SIDE0

log_pass
