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
# ident	"@(#)snapshot_005_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_005_pos
#
# DESCRIPTION:
# to the originally snapshot'd file system, after the file
# system has been changed. Uses 'sum -r'.
#
# STRATEGY:
# 1) Create a file in the zfs dataset
# 2) Sum the file for later comparison
# 3) Create a snapshot of the dataset
# 4) Append to the original file
# 5) Verify both checksums match
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "both"

function cleanup
{
	snapexists $SNAPCTR
	if [[ $? -eq 0 ]]; then
		log_must $ZFS destroy $SNAPCTR
	fi

	if [[ -e $SNAPDIR1 ]]; then
		log_must $RM -rf $SNAPDIR1 > /dev/null 2>&1
	fi

	if [[ -e $TESTDIR ]]; then
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
	fi
}

log_assert "Verify that a snapshot of a dataset is identical to " \
    "the original dataset."
log_onexit cleanup

log_note "Create a file in the zfs filesystem..."
log_must $FILE_WRITE -o create -f $TESTDIR1/$TESTFILE -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

log_note "Sum the file, save for later comparison..."
FILE_SUM=`$SUM -r $TESTDIR1/$TESTFILE | $AWK  '{ print $1 }'`
log_note "FILE_SUM = $FILE_SUM"

log_note "Create a snapshot and mount it..."
log_must $ZFS snapshot $SNAPCTR

log_note "Append to the original file..."
log_must $FILE_WRITE -o append -f $TESTDIR1/$TESTFILE -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

SNAP_FILE_SUM=`$SUM -r $SNAPDIR1/$TESTFILE | $AWK '{ print $1 }'`
if [[ $SNAP_FILE_SUM -ne $FILE_SUM ]]; then
	log_fail "Sums do not match, aborting!! ($SNAP_FILE_SUM != $FILE_SUM)"
fi

log_pass "Both Sums match. ($SNAP_FILE_SUM == $FILE_SUM)"
