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
# ident	"@(#)history_005_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_005_neg
#
# DESCRIPTION:
# 	Verify the following zpool subcommands are not logged.
#       	zpool list
#		zpool status
#		zpool iostat 
#
# STRATEGY:
#	1. Create a test pool.
#	2. Separately invoke zpool list|status|iostat
#	3. Verify they was not recored in pool history.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	[[ -f $EXPECT_HISTORY ]] && $RM -f $EXPECT_HISTORY
	[[ -f $REAL_HISTORY ]] && $RM -f $REAL_HISTORY
}

log_assert "Verify 'zpool list|status|iostat' will not be logged."
log_onexit cleanup

# Save initial TESTPOOL history
log_must eval "$ZPOOL history $TESTPOOL > $EXPECT_HISTORY"

log_must $ZPOOL list $TESTPOOL > /dev/null
log_must $ZPOOL status $TESTPOOL > /dev/null
log_must $ZPOOL iostat $TESTPOOL > /dev/null

log_must eval "$ZPOOL history $TESTPOOL > $REAL_HISTORY"
log_must $DIFF $EXPECT_HISTORY $REAL_HISTORY

log_pass "Verify 'zpool list|status|iostat' passed."
