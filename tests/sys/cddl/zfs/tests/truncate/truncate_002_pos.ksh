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
# ident	"@(#)truncate_002_pos.ksh	1.2	07/01/09 SMI"
#

. ${STF_SUITE}/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: truncate_002_pos
#
# DESCRIPTION:
# Tests file truncation within ZFS while a sync operation is in progress.
#
# STRATEGY:
# 1. Copy a file to ZFS filesystem
# 2. Copy /dev/null to same file on ZFS filesystem
# 3. Execute a sync command
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

function cleanup
{
	[[ -e $TESTDIR ]] && log_must $RM -rf ${TESTDIR}/*
}

log_assert "Ensure zeroed file gets written correctly during a sync operation"

srcfilename="$STF_SUITE/include/libtest.kshlib"

log_onexit cleanup

log_note "Copying $srcfilename to $TESTFILE"
log_must $CP $srcfilename ${TESTDIR}/${TESTFILE}

log_note "Copying /dev/null to $TESTFILE"
log_must $CP /dev/null ${TESTDIR}/${TESTFILE}

log_note "Now 'sync' the filesystem"
(cd $TESTDIR; log_must $SYNC)

log_pass "Successful truncation within ZFS while a sync operation is in progress."
