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
# ident	"@(#)slog_013_pos.ksh	1.2	09/05/19 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_013_pos
#
# DESCRIPTION:
#	Verify slog device can be disk, file, lofi device or any device that
#	presents a block interface.
#
# STRATEGY:
#	1. Create a pool
#	2. Loop to add different object as slog
#	3. Verify it passes
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup_testenv
{
	cleanup
	if datasetexists $TESTPOOL2 ; then
		log_must $ZPOOL destroy -f $TESTPOOL2
	fi
	if [[ -n $lofidev ]]; then
		$LOFIADM -d $lofidev
	fi
}

log_assert "Verify slog device can be disk, file, lofi device or any device " \
	"that presents a block interface."
log_onexit cleanup_testenv

dsk1=${DISKS%% *}
log_must $ZPOOL create $TESTPOOL ${DISKS#$dsk1}

# Add nomal disk
log_must $ZPOOL add $TESTPOOL log $dsk1
log_must verify_slog_device $TESTPOOL $dsk1 'ONLINE'
# Add nomal file
log_must $ZPOOL add $TESTPOOL log $LDEV
ldev=$(random_get $LDEV)
log_must verify_slog_device $TESTPOOL $ldev 'ONLINE'

# Add lofi device
lofidev=${LDEV2%% *}
log_must $LOFIADM -a $lofidev
lofidev=$($LOFIADM $lofidev)
log_must $ZPOOL add $TESTPOOL log $lofidev
log_must verify_slog_device $TESTPOOL $lofidev 'ONLINE'

log_pass "Verify slog device can be disk, file, lofi device or any device " \
	"that presents a block interface."

# Temp disable fore bug 6569095
# Add file which reside in the itself
mntpnt=$(get_prop mountpoint $TESTPOOL)
log_must create_vdevs $mntpnt/vdev
log_must $ZPOOL add $TESTPOOL $mntpnt/vdev

# Temp disable fore bug 6569072
# Add ZFS volume
vol=$TESTPOOL/vol
log_must $ZPOOL create -V 64M $vol
log_must $ZPOOL add $TESTPOOL /dev/zvol/$vol
