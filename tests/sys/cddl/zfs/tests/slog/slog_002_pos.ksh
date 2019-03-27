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
# ident	"@(#)slog_002_pos.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_002_pos
#
# DESCRIPTION:
#	Adding a log device to normal pool works.
#
# STRATEGY:
#	1. Create pool
#	2. Add log devices with different configuration
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

log_assert "Adding a log device to normal pool works."
log_onexit cleanup

function test_adding_slog # <pooltype> <sparetype> <logtype>
{
	create_pool $TESTPOOL $pooltype $VDEV $sparetype $SDEV
	log_must $ZPOOL add $TESTPOOL log $logtype $LDEV
	log_must display_status $TESTPOOL
	typeset ldev=$(random_get $LDEV)
	log_must verify_slog_device $TESTPOOL $ldev ONLINE $logtype
	destroy_pool $TESTPOOL
}

slog_foreach_all test_adding_slog

log_pass "Adding a log device to normal pool works."
