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
# ident	"@(#)slog_009_neg.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_009_neg
#
# DESCRIPTION:
#	A raidz/raidz2 log can not be added to existed pool.
#
# STRATEGY:
#	1. Create pool with or without log.
#	2. Add a raidz/raidz2 log to this pool.
#	3. Verify failed to add.
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

log_assert "A raidz/raidz2 log can not be added to existed pool."
log_onexit cleanup

function test_no_raidz_slog_add # <pooltype> <sparetype> <logtype>
{
	typeset pooltype=$1
	typeset sparetype=$2
	typeset logtype=$3

	log=$(random_get_with_non "log")
	create_pool $TESTPOOL $pooltype $VDEV $sparetype $SDEV $logtype $LDEV

	log_mustnot $ZPOOL add $TESTPOOL log $logtype $LDEV2
	ldev=$(random_get $LDEV2)
	log_mustnot verify_slog_device $TESTPOOL $ldev 'ONLINE' $logtype
	destroy_pool $TESTPOOL
}

log_pass "A raidz/raidz2 log can not be added to existed pool."
