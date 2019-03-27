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
# ident	"@(#)rollback_002_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: rollback_002_pos
#
# DESCRIPTION:
# Verify that rollbacks are with respect to the latest snapshot.
#
# STRATEGY:
# 1. Empty a file system
# 2. Populate the file system
# 3. Take a snapshot of the file system
# 4. Add new files to the file system
# 5. Take a snapshot
# 6. Remove the original files
# 7. Perform a rollback
# 8. Verify the latest snapshot and file system agree
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
	snapexists $SNAPFS.1
	[[ $? -eq 0 ]] && \
		log_must $ZFS destroy $SNAPFS.1

	snapexists $SNAPFS
	[[ $? -eq 0 ]] && \
		log_must $ZFS destroy $SNAPFS

	[[ -e $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
}

log_assert "Verify rollback is with respect to latest snapshot."

log_onexit cleanup

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to first snapshot)"
populate_dir $TESTDIR/original_file $COUNT $NUM_WRITES $BLOCKSZ ITER

log_must $ZFS snapshot $SNAPFS

FILE_COUNT=`$LS -Al $SNAPDIR | $GREP -v "total" | wc -l`
if [[ $FILE_COUNT -ne $COUNT ]]; then
        $LS -Al $SNAPDIR
        log_fail "AFTER: $SNAPFS contains $FILE_COUNT files(s)."
fi

log_note "Populate the $TESTDIR directory (prior to second snapshot)"
populate_dir $TESTDIR/afterfirst_file $COUNT $NUM_WRITES $BLOCKSZ ITER
log_must $ZFS snapshot $SNAPFS.1

log_note "Populate the $TESTDIR directory (Post second snapshot)"
populate_dir $TESTDIR/aftersecond_file $COUNT $NUM_WRITES $BLOCKSZ ITER

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/original_file* > /dev/null 2>&1

#
# Now rollback to latest snapshot
#
log_must $ZFS rollback $SNAPFS.1

FILE_COUNT=`$LS -Al $TESTDIR/aftersecond* 2> /dev/null \
    | $GREP -v "total" | wc -l`
if [[ $FILE_COUNT -ne 0 ]]; then
        $LS -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT aftersecond* files(s)."
fi

FILE_COUNT=`$LS -Al $TESTDIR/original* $TESTDIR/afterfirst*| $GREP -v "total" | wc -l`
if [[ $FILE_COUNT -ne 20 ]]; then
        $LS -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT original* files(s)."
fi

log_pass "The rollback to the latest snapshot succeeded."
