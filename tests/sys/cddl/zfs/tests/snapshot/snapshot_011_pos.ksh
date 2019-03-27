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
# ident	"@(#)snapshot_011_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_011_pos
#
# DESCRIPTION:
#	use 'snapshot -r' to create a snapshot tree, add some files to one child 
#	filesystem, rollback the child filesystem snapshot, verify that the child
# 	filesystem gets back to the status while taking the snapshot.	
#
# STRATEGY:
#	1. Add some files to a target child filesystem
#	2. snapshot -r the parent filesystem
#	3. Add some other files to the target child filesystem
#	4. rollback the child filesystem snapshot
#	5. verify that the child filesystem get back to the status while being 
#	   snapshot'd
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	snapexists $SNAPPOOL && \
		log_must $ZFS destroy -r $SNAPPOOL

	[[ -e $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
}

log_assert "Verify that rollback to a snapshot created by snapshot -r succeeds."
log_onexit cleanup

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to snapshot)"
populate_dir $TESTDIR/before_file $COUNT $NUM_WRITES $BLOCKSZ ITER

log_must $ZFS snapshot -r $SNAPPOOL

FILE_COUNT=`$LS -Al $SNAPDIR | $GREP -v "total" | wc -l`
if (( FILE_COUNT != COUNT )); then
        $LS -Al $SNAPDIR
        log_fail "AFTER: $SNAPFS contains $FILE_COUNT files(s)."
fi

log_note "Populate the $TESTDIR directory (post snapshot)"
typeset -i i=0
populate_dir $TESTDIR/after_file $COUNT $NUM_WRITES $BLOCKSZ ITER

#
# Now rollback to latest snapshot
#
log_must $ZFS rollback $SNAPFS

FILE_COUNT=`$LS -Al $TESTDIR/after* 2> /dev/null | $GREP -v "total" | wc -l`
if (( FILE_COUNT != 0 )); then
        $LS -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT after* files(s)."
fi

FILE_COUNT=`$LS -Al $TESTDIR/before* 2> /dev/null \
    | $GREP -v "total" | wc -l`
if (( FILE_COUNT != $COUNT )); then
	$LS -Al $TESTDIR
	log_fail "$TESTDIR contains $FILE_COUNT before* files(s)."
fi

log_pass "Rollback with child snapshot works as expected."
