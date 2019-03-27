#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)zvol_misc_002_pos.ksh	1.4	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_002_pos
#
# DESCRIPTION:
# Verify that ZFS volume snapshot could be fscked
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Copy some files and create snapshot
# 3. Verify fsck on the snapshot is OK
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

verify_runnable "global"

function cleanup
{
	snapexists $TESTPOOL/$TESTVOL@snap && \
		$ZFS destroy $TESTPOOL/$TESTVOL@snap

	ismounted $TESTDIR ufs && log_must $UMOUNT $TESTDIR
	[[ -e $TESTDIR ]] && $RM -rf $TESTDIR
}

log_assert "Verify that ZFS volume snapshot could be fscked"
log_onexit cleanup

$NEWFS /dev/zvol/$TESTPOOL/$TESTVOL >/dev/null 2>&1
(( $? != 0 )) && log_fail "Unable to newfs(1M) $TESTPOOL/$TESTVOL"

log_must $MKDIR $TESTDIR
log_must $MOUNT /dev/zvol/$TESTPOOL/$TESTVOL $TESTDIR

typeset -i fn=0
typeset -i retval=0

# Write about 200MB of data.
populate_dir $TESTDIR/testfile 5 $NUM_WRITES $BLOCKSZ 0

log_must sync
log_must $MOUNT -o rw -u $TESTDIR
log_must $ZFS snapshot $TESTPOOL/$TESTVOL@snap
log_must $FSCK -t ufs -n /dev/zvol/$TESTPOOL/$TESTVOL@snap >/dev/null 2>&1

log_pass "Verify that ZFS volume snapshot could be fscked"
