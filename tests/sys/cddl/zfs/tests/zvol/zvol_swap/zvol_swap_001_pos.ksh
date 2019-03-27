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
# ident	"@(#)zvol_swap_001_pos.ksh	1.3	08/05/14 SMI"
#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_swap_001_pos
#
# DESCRIPTION:
# Verify that a zvol can be used as a swap device
#
# STRATEGY:
# 1. Create a pool
# 2. Create a zvol volume
# 3. Use zvol as swap space
# 4. Create a file under $TMPDIR
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
	$RM -rf $TMPDIR/$TESTFILE
	
	if is_swap_inuse $voldev ; then
		log_must $SWAP -d $voldev
	fi
}

log_assert "Verify that a zvol can be used as a swap device"

log_onexit cleanup

test_requires SWAP

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
log_note "Add zvol volume as swap space"
log_must $SWAP -a $voldev

log_note "Create a file under $TMPDIR"
log_must $FILE_WRITE -o create -f $TMPDIR/$TESTFILE \
    -b $BLOCKSZ -c $NUM_WRITES -d $DATA

[[ ! -f $TMPDIR/$TESTFILE ]] &&
    log_fail "Unable to create file under $TMPDIR"

filesize=`$LS -l $TMPDIR/$TESTFILE | $AWK '{print $5}'`
tf_size=$(( BLOCKSZ * NUM_WRITES ))
(( $tf_size != $filesize )) &&
    log_fail "testfile is ($filesize bytes), expected ($tf_size bytes)"

log_pass "Successfully added a zvol to swap area."
