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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2012,2013 Spectra Logic Corporation.  All rights reserved.
# Use is subject to license terms.
# 
# Portions taken from:
# ident	"@(#)replacement_001_pos.ksh	1.4	08/02/27 SMI"
#
# $FreeBSD$

. $STF_SUITE/tests/hotspare/hotspare.kshlib
. $STF_SUITE/tests/zfsd/zfsd.kshlib
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/include/libgnop.kshlib

log_assert "ZFSD will automatically replace a SAS disk that disappears and reappears in the same location, with the same devname"

ensure_zfsd_running

set_disks

typeset REMOVAL_DISK=$DISK0
typeset REMOVAL_NOP=${DISK0}.nop
typeset OTHER_DISKS="${DISK1} ${DISK2}"
typeset ALLDISKS="${DISK0} ${DISK1} ${DISK2}"
typeset ALLNOPS=${ALLDISKS//~(E)([[:space:]]+|$)/.nop\1}

log_must create_gnops $ALLDISKS
for type in "raidz" "mirror"; do
	# Create a pool on the supplied disks
	create_pool $TESTPOOL $type $ALLNOPS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	# Disable the first disk.
	log_must destroy_gnop $REMOVAL_DISK

	# Write out data to make sure we can do I/O after the disk failure
	log_must $DD if=/dev/zero of=$TESTDIR/$TESTFILE bs=1m count=1
	log_must $FSYNC $TESTDIR/$TESTFILE

	# Check to make sure ZFS sees the disk as removed
	wait_for_pool_dev_state_change 20 $REMOVAL_NOP REMOVED

	# Re-enable the disk
	log_must create_gnop $REMOVAL_DISK

	# Disk should auto-join the zpool & be resilvered.
	wait_for_pool_dev_state_change 20 $REMOVAL_NOP ONLINE
	wait_until_resilvered

	$ZPOOL status $TESTPOOL
	destroy_pool $TESTPOOL
	log_must $RM -rf /$TESTPOOL
done

log_pass
