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
# ident	"@(#)rollback_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: rollback_001_pos
#
# DESCRIPTION:
# Populate a file system and take a snapshot. Add some more files to the
# file system and rollback to the last snapshot. Verify no post snapshot
# file exist.
#
# STRATEGY:
# 1. Empty a file system
# 2. Populate the file system
# 3. Take a snapshot of the file system
# 4. Add new files to the file system
# 5. Perform a rollback
# 6. Verify the snapshot and file system agree
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
	snapexists $SNAPFS
	[[ $? -eq 0 ]] && \
		log_must $ZFS destroy $SNAPFS

	[[ -e $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
}

log_assert "Verify that a rollback to a previous snapshot succeeds."

log_onexit cleanup

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to snapshot)"
populate_dir $TESTDIR/before_file $COUNT $NUM_WRITES $BLOCKSZ ITER
log_must $ZFS snapshot $SNAPFS

FILE_COUNT=`$LS -Al $SNAPDIR | $GREP -v "total" | wc -l`
if [[ $FILE_COUNT -ne $COUNT ]]; then
        $LS -Al $SNAPDIR
        log_fail "AFTER: $SNAPFS contains $FILE_COUNT files(s)."
fi

log_note "Populate the $TESTDIR directory (post snapshot)"
populate_dir $TESTDIR/after_file $COUNT $NUM_WRITES $BLOCKSZ ITER

#
# Now rollback to latest snapshot
#
log_must $ZFS rollback $SNAPFS

FILE_COUNT=`$LS -Al $TESTDIR/after* 2> /dev/null | $GREP -v "total" | wc -l`
if [[ $FILE_COUNT -ne 0 ]]; then
        $LS -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT after* files(s)."
fi

FILE_COUNT=`$LS -Al $TESTDIR/before* 2> /dev/null \
    | $GREP -v "total" | wc -l`
if [[ $FILE_COUNT -ne $COUNT ]]; then
	$LS -Al $TESTDIR
	log_fail "$TESTDIR contains $FILE_COUNT before* files(s)."
fi

log_pass "The rollback operation succeeded."
