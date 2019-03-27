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
# ident	"@(#)zfs_unallow_001_neg.ksh	1.2	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unallow_001_neg
#
# DESCRIPTION:
#
# zfs unallow returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to unallow a set of permissions
# 2. Verify the unallow wasn't performed
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

# check to see if we have zfs unallow
$ZFS 2>&1 | $GREP "unallow" > /dev/null
if (($? != 0)) then
        log_unsupported "ZFS unallow not supported on this machine."
fi

log_assert "zfs unallow returns an error when run as a user"

log_mustnot run_unprivileged "$ZFS unallow everyone $TESTPOOL/$TESTFS/allowed"

# now check with zfs allow to see if the permissions are still there
OUTPUT=$($ZFS allow $TESTPOOL/$TESTFS/allowed | $GREP "Local+Descendent" )
if [ -z "$OUTPUT" ]
then
	log_fail "Error - create permissions were unallowed on \
	$TESTPOOL/$TESTFS/allowed"
fi

log_pass "zfs unallow returns an error when run as a user"

