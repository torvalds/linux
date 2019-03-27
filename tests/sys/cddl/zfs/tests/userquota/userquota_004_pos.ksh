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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)userquota_004_pos.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_004_pos
#
# DESCRIPTION:
#       Check the basic function user|group used
#
#
# STRATEGY:
#       1. Write some data to fs by normal user and check the user|group used
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
###############################################################################

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "Check the basic function of {user|group} used"

typeset user_used=$(get_value "userused@$QUSER1" $QFS)
typeset group_used=$(get_value "groupused@$QGROUP" $QFS)

if [[ $user_used != 0 ]]; then
	log_fail "FAIL: userused should be 0"
fi
if [[ $group_used != 0 ]]; then
	log_fail "FAIL: groupused should be 0"
fi

mkmount_writable $QFS
log_must user_run $QUSER1 $TRUNCATE -s 100m $QFILE
$SYNC

user_used=$(get_value "userused@$QUSER1" $QFS)
group_used=$(get_value "groupused@$QGROUP" $QFS)

if [[ $user_used != "100M" ]]; then
	log_note "user $QUSER1 used is $user_used"
	log_fail "userused for user $QUSER1 expected to be 50.0M, not $user_used"
fi

if [[ $user_used != $group_used ]]; then
	log_note "user $QUSER1 used is $user_used"
	log_note "group $QGROUP used is $group_used"
	log_fail "FAIL: userused should equal to groupused"
fi

log_pass "Check the basic function of {user|group}used pass as expect"
