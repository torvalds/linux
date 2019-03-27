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
# Copyright 2011 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#

. ${STF_SUITE}/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: transaction_group_integrity_001_pos
#
# DESCRIPTION:
#
# Verify the integrity of non-aligned writes to the same blocks that cross
# transaction groups.
#
# STRATEGY:
# This test verifies that non-aligned writes are correctly committed to the
# file system, even adjacent transaction groups include writes to the same
# blocks.  The test runs through multiple repetitions in an attempt to trigger
# race conditions.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: BEGIN (2011-10-20)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "both"

log_assert "Ensure that non-aligned writes to the same blocks that cross" \
  "transaction groups do not corrupt the file."

# Run the test program
txg_integrity ${TESTDIR}/${TESTFILE}

# Success is indicated by the return status
if [[ $? -ne 0 ]]; then
  log_fail "Test failed to execute or file became corrupted"
else
  log_pass "Multiple unaligned writes from multiple transactions groups succeeded"
fi


