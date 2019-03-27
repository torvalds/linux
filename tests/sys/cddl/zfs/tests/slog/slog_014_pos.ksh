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
# ident	"@(#)slog_014_pos.ksh	1.1	09/06/22 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_014_pos
#
# DESCRIPTION:
#	log device can survive when one of pool device get corrupted
#
# STRATEGY:
#	1. Create pool with slog devices
#	2. remove one disk
#	3. Verify the log is fine
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-05-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "log device can survive when one of the pool device get corrupted."
log_onexit cleanup

function test_slog_survives_pool_corruption # <pooltype> <sparetype>
{
	typeset pooltype=$1
	typeset sparetype=$2

	# This only works for pools that have redundancy
	[ -z "$pooltype" ] && return

	create_pool $TESTPOOL $pooltype $VDEV $sparetype $SDEV log $LDEV 

	# Remove one of the pool device, then scrub to make the pool DEGRADED
	log_must $RM -f $VDIR/a
	log_must $ZPOOL scrub $TESTPOOL

	# Check and verify pool status
	log_must display_status $TESTPOOL
	log_must $ZPOOL status $TESTPOOL 2>&1 >/dev/null

	# Check that there is some status: field informing us of a
	# problem.  The exact error string is unspecified.
	log_must $ZPOOL status -v $TESTPOOL | $GREP "status:"
	for l in $LDEV; do
		log_must check_state $TESTPOOL $l ONLINE
	done

	destroy_pool $TESTPOOL
	create_vdevs $VDIR/a
}
slog_foreach_nologtype test_slog_survives_pool_corruption

log_pass "log device can survive when one of the pool device get corrupted."
