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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)slog_012_neg.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_012_neg
#
# DESCRIPTION:
#	Pool can survive when one of mirror log device get corrupted
#
# STRATEGY:
#	1. Create pool with mirror slog devices
#	2. Make corrupted on one disk
#	3. Verify the pool is fine
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

log_assert "Pool can survive when one of mirror log device get corrupted."
log_onexit cleanup

function test_slog_mirror_corruption # <pooltype> <sparetype>
{
	typeset pooltype=$1
	typeset sparetype=$2

	create_pool $TESTPOOL $type $VDEV $spare $SDEV log mirror $LDEV 

	mntpnt=$(get_prop mountpoint $TESTPOOL)
	#
	# Create file in pool to trigger writing in slog devices
	#
	log_must $DD if=/dev/urandom of=$mntpnt/testfile.${TESTCASE_ID} count=100

	ldev=$(random_get $LDEV)
	log_must create_vdevs $ldev
	log_must $ZPOOL scrub $TESTPOOL

	log_must display_status $TESTPOOL
	log_must verify_slog_device $TESTPOOL $ldev UNAVAIL mirror

	destroy_pool $TESTPOOL
}
slog_foreach_nologtype test_slog_mirror_corruption

log_pass "Pool can survive when one of mirror log device get corrupted."
