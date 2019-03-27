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
# ident	"@(#)zfs_upgrade_001_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_upgrade_001_neg
#
# DESCRIPTION:
#
# zfs upgrade returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to upgrade a version1 dataset
# 2. Verify the dataset wasn't upgraded
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

# check to see if we have upgrade capability
$ZFS upgrade > /dev/null 2>&1
HAS_UPGRADE=$?
if [ $HAS_UPGRADE -ne 0 ]
then
	log_unsupported "Zfs upgrade not supported"
fi

log_assert "zfs upgrade returns an error when run as a user"


log_mustnot run_unprivileged "$ZFS upgrade $TESTPOOL/$TESTFS/version1"

# now check to see the above command didn't do anything
VERSION=$($ZFS upgrade $TESTPOOL/$TESTFS/version1 2>&1 \
	 | $GREP "already at this version")
if [ -n "$VERSION" ]
then
	log_fail "A filesystem was upgraded!"
fi

log_pass "zfs upgrade returns an error when run as a user"

