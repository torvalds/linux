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
# ident	"@(#)zpool_export_001_pos.ksh	1.3	09/01/12 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_export_001_pos
#
# DESCRIPTION:
# Exported pools should no longer be visible from 'zpool list'.
# Therefore, we export an existing pool and verify it cannot
# be accessed.
#
# STRATEGY:
# 1. Unmount the test directory.
# 2. Export the pool.
# 3. Verify the pool is no longer present in the list output.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset dir=$(get_device_dir $DISKS)

	datasetexists "$TESTPOOL/$TESTFS" || \
		log_must $ZPOOL import -d $dir $TESTPOOL

	ismounted "$TESTPOOL/$TESTFS"
	(( $? != 0 )) && \
	    log_must $ZFS mount $TESTPOOL/$TESTFS
}

log_onexit cleanup

log_assert "Verify a pool can be exported."

log_must $ZFS umount $TESTDIR
log_must $ZPOOL export $TESTPOOL

poolexists $TESTPOOL && \
        log_fail "$TESTPOOL unexpectedly found in 'zpool list' output."

log_pass "Successfully exported a ZPOOL."
