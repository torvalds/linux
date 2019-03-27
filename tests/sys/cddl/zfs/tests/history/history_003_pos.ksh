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
# ident	"@(#)history_003_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_003_pos
#
# DESCRIPTION:
#	zpool history can record and output huge log.
#
# STRATEGY:
#	1. Create two 100M virtual disk files.
#	2. Create test pool using the two virtual files.
#	3. Loop N times to set compression to test pool.
#	4. Make sure 'zpool history' output correctly.
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

log_assert "zpool history limitation test."

typeset -i expect_count=300
typeset -i orig_count=$($ZPOOL history $TESTPOOL | $WC -l | $AWK '{print $1}')

typeset -i i=0
typeset -i num_iters=0
((num_iters = expect_count / 5))
while ((i < num_iters)); do
	$ZFS set compression=off $TESTPOOL/$TESTFS
	$ZFS set compression=on $TESTPOOL/$TESTFS
	$ZFS set compression=off $TESTPOOL/$TESTFS
	$ZFS set compression=on $TESTPOOL/$TESTFS
	$ZFS set compression=off $TESTPOOL/$TESTFS

	((i += 1))
done

typeset -i entry_count=$($ZPOOL history $TESTPOOL | $WC -l | $AWK '{print $1}')

typeset -i count_diff=0
((count_diff = entry_count - orig_count))
if ((count_diff != expect_count)); then
	echo "Zpool history is as follows:"
	log_must $ZPOOL history $TESTPOOL
	log_fail "Expected $expect_count new entries, got $count_diff"
fi

log_pass "zpool history limitation test passed."
