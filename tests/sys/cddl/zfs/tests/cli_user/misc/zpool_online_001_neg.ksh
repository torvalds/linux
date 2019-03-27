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
# ident	"@(#)zpool_online_001_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_online_001_neg
#
# DESCRIPTION:
#
# zpool online returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to online a device in a pool
# 2. Verify the command fails
#
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-27)
#
# __stc_assertion_end
#
################################################################################

function check_for_online
{
	RESULT=$($ZPOOL status -v $TESTPOOL.virt | $GREP disk-offline.dat \
		 | $GREP ONLINE )
	if [ -n "$RESULT" ]
	then
		log_fail "A disk was brough online!"
	fi
}

verify_runnable "global"

log_assert "zpool online returns an error when run as a user"

log_mustnot run_unprivileged "$ZPOOL online $TESTPOOL.virt /$TESTDIR/disk-offline.dat"
check_for_online

log_mustnot run_unprivileged "$ZPOOL online -t $TESTPOOL.virt /$TESTDIR/disk-offline.dat"
check_for_online

log_pass "zpool online returns an error when run as a user"

