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
# ident	"@(#)slog_007_pos.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_007_pos
#
# DESCRIPTION:
#	Exporting and importing pool with log devices passes.
#
# STRATEGY:
#	1. Create pool with log devices.
#	2. Export and import the pool
#	3. Display pool status
#	4. Destroy and import the pool again
#	5. Display pool status
#	6. Destroy and loop to create pool with different configuration.
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

log_assert "Exporting and importing pool with log devices passes."
log_onexit cleanup

function test_slog_exporting_importing # <pooltype> <sparetype> <logtype>
{
	typeset pooltype=$1
	typeset sparetype=$2
	typeset logtype=$3

	create_pool $TESTPOOL $pooltype $VDEV $sparetype $SDEV \
		log $logtype $LDEV $LDEV2
	ldev=$(random_get $LDEV $LDEV2)
	log_must verify_slog_device $TESTPOOL $ldev ONLINE $logtype
	log_must $ZPOOL export $TESTPOOL
	log_must $ZPOOL import -d $VDIR -d $VDIR2 $TESTPOOL
	log_must display_status $TESTPOOL
	ldev=$(random_get $LDEV $LDEV2)
	log_must verify_slog_device $TESTPOOL $ldev ONLINE $logtype

	destroy_pool $TESTPOOL
	log_must $ZPOOL import -Df -d $VDIR -d $VDIR2 $TESTPOOL
	log_must display_status $TESTPOOL
	ldev=$(random_get $LDEV $LDEV2)
	log_must verify_slog_device $TESTPOOL $ldev ONLINE $logtype
	destroy_pool $TESTPOOL
}
slog_foreach_all test_slog_exporting_importing

log_pass "Exporting and importing pool with log devices passes."
