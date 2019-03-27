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
# ident	"@(#)large_files_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: large_files_001_pos
#
# DESCRIPTION:
# Write a file to the allowable ZFS fs size.
#
# STRATEGY:
# 1. largest_file will write to a file and increase its size
# to the maximum allowable.
# 2. The last byte of the file should be accessbile without error.
# 3. Writing beyond the maximum file size generates an 'errno' of
# EFBIG.
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

log_assert "Write a file to the allowable ZFS fs size."

log_note "Invoke 'largest_file' with $TESTDIR/$TESTFILE"
log_must $LARGEST_FILE $TESTDIR/$TESTFILE

log_pass "Successfully created a file to the maximum allowable size."
