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
# ident	"@(#)history_004_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_004_pos
#
# DESCRIPTION:
#	'zpool history' can copes with many simultaneous command.
#
# STRATEGY:
#	1. Create test pool and test fs.
#	2. Loop 100 times, set properties to test fs simultaneously.
#	3. Wait for all the command execution complete.
#	4. Make sure all the commands was logged by 'zpool history'.
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

log_assert "'zpool history' can copes with many simultaneous command."

typeset -i orig_count=$($ZPOOL history $TESTPOOL | $WC -l | $AWK '{print $1}')

typeset -i i=0
while ((i < 10)); do
	$ZFS set compression=off $TESTPOOL/$TESTFS &
	$ZFS set atime=off $TESTPOOL/$TESTFS &
	$ZFS create $TESTPOOL/$TESTFS1 &
	$ZFS create $TESTPOOL/$TESTFS2 &
	$ZFS create $TESTPOOL/$TESTFS3 &
	
	wait

	$ZFS snapshot $TESTPOOL/$TESTFS1@snap & 
	$ZFS snapshot $TESTPOOL/$TESTFS2@snap & 
	$ZFS snapshot $TESTPOOL/$TESTFS3@snap & 

	wait

	$ZFS clone $TESTPOOL/$TESTFS1@snap $TESTPOOL/clone1 &
	$ZFS clone $TESTPOOL/$TESTFS2@snap $TESTPOOL/clone2 &
	$ZFS clone $TESTPOOL/$TESTFS3@snap $TESTPOOL/clone3 &

	wait 

	$ZFS promote $TESTPOOL/clone1 &
	$ZFS promote $TESTPOOL/clone2 &
	$ZFS promote $TESTPOOL/clone3 &

	wait

	$ZFS destroy $TESTPOOL/$TESTFS1 &
	$ZFS destroy $TESTPOOL/$TESTFS2 &
	$ZFS destroy $TESTPOOL/$TESTFS3 &

	wait

	$ZFS destroy -Rf $TESTPOOL/clone1 &
	$ZFS destroy -Rf $TESTPOOL/clone2 &
	$ZFS destroy -Rf $TESTPOOL/clone3 &

	wait
	((i += 1))
done

typeset -i count=$($ZPOOL history $TESTPOOL | $WC -l | $AWK '{print $1}')

if ((count - orig_count != 200)); then
	$ZPOOL history $spool
	log_fail "Expected 200 more than $orig_count entries, but got $count"
fi

log_pass "zpool history copes with simultaneous commands passed."
