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
# ident	"@(#)setup.ksh	1.4	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib

verify_runnable "global"

DISK=${DISKS%% *}

for dev in $ZFS_DISK1 $ZFS_DISK2 ; do
	log_must cleanup_devices $dev
done

typeset -i i=0
if [[ $DISK_COUNT -lt 2 ]]; then
    partition_disk $PART_SIZE $ZFS_DISK1 $GROUP_NUM
fi

create_pool "$TESTPOOL" "$ZFSSIDE_DISK1"

if [[ -d $TESTDIR ]]; then
	$RM -rf $TESTDIR  || log_unresolved Could not remove $TESTDIR
	$MKDIR -p $TESTDIR || log_unresolved Could not create $TESTDIR
fi

log_must $ZFS create $TESTPOOL/$TESTFS
log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

# Limit the filesystem size to 32GiB; this should be sufficient.
(( MAXSECTS = 32 * 1024 * 1024 ))
NUMSECTS=`diskinfo ${ZFSSIDE_DISK2} | awk '{print $4}'`
if [[ $NUMSECTS -gt $MAXSECTS ]]; then
	NUMSECTS=$MAXSECTS
fi

$ECHO "y" | $NEWFS -s $NUMSECTS $ZFSSIDE_DISK2 >/dev/null 2>&1
(( $? != 0 )) &&
	log_untested "Unable to setup a UFS file system"

[[ ! -d $DEVICE_DIR ]] && \
	log_must $MKDIR -p $DEVICE_DIR

log_must $MOUNT $ZFSSIDE_DISK2 $DEVICE_DIR

i=0
while (( i < $MAX_NUM )); do
	log_must create_vdevs ${DEVICE_DIR}/${DEVICE_FILE}$i
	(( i = i + 1 ))
done

log_pass
