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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_iostat_002_pos.ksh	1.2	08/05/14 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_iostat_002_pos
#
# DESCRIPTION:
# Verify that 'zpool iostat [interval [count]' can be executed as non-root.
#
# STRATEGY:
# 1. set the interval=2 and  count=3
# 2. sleep 30 seconds
# 3. Verify that the output have 3 record.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

typeset tmpfile=$TMPDIR/zfsiostat.out.${TESTCASE_ID}
typeset -i stat_count=0

function cleanup
{
	if [[ -f $tmpfile ]]; then
		$RM -f $tmpfile
	fi
}

log_onexit cleanup
log_assert "zpool iostat [pool_name ...] [interval] [count]"

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

run_unprivileged $ZPOOL iostat $TESTPOOL 2 3 > $tmpfile 2>&1
stat_count=$($GREP $TESTPOOL $tmpfile | $WC -l)

if [[ $stat_count -ne 3 ]]; then
	log_fail "zpool iostat [pool_name] [interval] [count] failed"
fi

log_pass "zpool iostat [pool_name ...] [interval] [count] passed"
