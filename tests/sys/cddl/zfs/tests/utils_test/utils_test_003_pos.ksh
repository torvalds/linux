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
# ident	"@(#)utils_test_003_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/utils_test/utils_test.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: utils_test_003_pos
#
# DESCRIPTION:
# Ensure that the fsdb(1M) utility fails on a ZFS file system.
#
# STRATEGY:
# 1. Populate a ZFS directory with a number of files.
# 2. Run fsdb against the raw device.
# 3. Ensure it fails.
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

verify_runnable "global"

function cleanup
{
	$RM -rf $TESTDIR/*
}

log_onexit cleanup

log_assert "Ensure that the fsdb(1M) utility fails on a ZFS file system."

populate_dir $TESTDIR/$TESTFILE $NUM_FILES $WRITE_COUNT $BLOCKSZ $DATA
inode_num=`$LS -li $TESTDIR/$TESTFILE.0 | $AWK '{print $1}'`
[[ -z $inode_num ]] && \
    log_fail "Failed to determine inode of file: $TESTDIR/$TESTFILE.0"

log_mustnot $ECHO ":inode $inode_num" | $FSDB ${DISK}a

log_pass "fsdb(1M) returned an error as expected."
