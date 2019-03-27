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
# ident	"@(#)slog_004_pos.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_004_pos
#
# DESCRIPTION:
#	Attaching a log device passes.
#
# STRATEGY:
#	1. Create pool with separated log devices.
#	2. Attaching a log device for existing log device
#	3. Display pool status
#	4. Destroy and loop to create pool with different configuration.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Attaching a log device passes."
log_onexit cleanup

function test_attaching_slog # <pooltype> <sparetype> <logtype>
{
	typeset pooltype=$1
	typeset sparetype=$2
	typeset logtype=$3

	create_pool $TESTPOOL $pooltype $VDEV $sparetype $SDEV \
		log $logtype $LDEV
	typeset ldev=$(random_get $LDEV)
	typeset ldev2=$(random_get $LDEV2)
	log_must $ZPOOL attach $TESTPOOL $ldev $ldev2
	log_must display_status $TESTPOOL
	log_must verify_slog_device $TESTPOOL $ldev ONLINE mirror
	log_must verify_slog_device $TESTPOOL $ldev2 ONLINE mirror
	destroy_pool $TESTPOOL
}
slog_foreach_all test_attaching_slog

log_pass "Attaching a log device passes."
