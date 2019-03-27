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
# ident	"@(#)slog_003_pos.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_003_pos
#
# DESCRIPTION:
#	Adding an extra log device works
#
# STRATEGY:
#	1. Create pool with separated log devices.
#	2. Add an extra log devices
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

log_assert "Adding an extra log device works."
log_onexit cleanup

function test_adding_extra_slog # <pooltype> <sparetype> <logtype>
{
	typeset pooltype=$1
	typeset sparetype=$2
	typeset logtype=$3

	for newtype in "" "mirror"; do
		create_pool $TESTPOOL $pooltype $VDEV $sparetype $SDEV \
			log $logtype $LDEV
		log_must $ZPOOL add $TESTPOOL log $newtype $LDEV2
		log_must display_status $TESTPOOL
		ldev=$(random_get $LDEV2)
		log_must verify_slog_device $TESTPOOL $ldev ONLINE $newtype
		destroy_pool $TESTPOOL
	done
}

slog_foreach_all test_adding_extra_slog

log_pass "Adding an extra log device works."
