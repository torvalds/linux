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
# Copyright 2013 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#

. ${STF_SUITE}/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: fsync_integrity_001_pos
#
# DESCRIPTION:
#
# Verify the integrity of non-aligned writes to the same blocks within the same
# transaction group, where an fsync is issued by a non-final writer.
#
# STRATEGY:

# This test verifies that the unoverride in the following sequence of events is
# handled correctly:
#
# 1) A new transaction group opens
# 2) A write is issued to a certain block
# 3) The writer fsyncs() that file
# 4) TBD module immediately writes that block, then places an override in the
#    syncer's TBD data structure, indicating that it doesn't need to write that
#    block when syncing.
# 5) Another write is issued to the same block, with different data.
# 6) TBD module unoverrides that block in the syncer's TBD data structure
# 7) The syncer writes that block 
#
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: BEGIN (2013-1-21)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "both"

log_assert "Verify the integrity of non-aligned writes to the same blocks within the same transaction group, where an fsync is issued by a non-final writer."

# Run the test program
fsync_integrity ${TESTDIR}/${TESTFILE}

# Success is indicated by the return status
if [[ $? -ne 0 ]]; then
  log_fail "Test failed to execute or file became corrupted"
else
  log_pass
fi


