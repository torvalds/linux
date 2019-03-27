#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)quota_003_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/quota/quota.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: quota_003_pos
#
# DESCRIPTION:
# A ZFS file system quota limits the amount of pool space
# available to a file system dataset. Apply a quota and verify
# that no more file creates are permitted.
#
# NOTE: THis test applies to a dataset rather than a file system.
#
# STRATEGY:
# 1) Apply quota to ZFS file system dataset
# 2) Create a file which is larger than the set quota
# 3) Verify that the resulting file size is less than the quota limit
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify that file size is limited by the file system quota" \
    "(dataset version)"

#
# cleanup to be used internally as otherwise quota assertions cannot be
# run independently or out of order
#
function cleanup
{
	[[ -e $TESTDIR1/$TESTFILE1 ]] && \
	    log_must $RM $TESTDIR1/$TESTFILE1

	#
        # Need to allow time for space to be released back to
        # pool, otherwise next test will fail trying to set a
        # quota which is less than the space used.
        #
        sleep 5
        log_must $ZFS set quota=none $TESTPOOL/$TESTFS
}

log_onexit cleanup

#
# Sets the quota value and attempts to fill it with a file
# twice the size of the quota
#
log_must fill_quota $TESTPOOL/$TESTCTR/$TESTFS1 $TESTDIR1

log_pass "File size limited by quota"
