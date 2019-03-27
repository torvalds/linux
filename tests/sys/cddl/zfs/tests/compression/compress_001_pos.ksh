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
# ident	"@(#)compress_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: compress_001_pos
#
# DESCRIPTION:
# Create two files of exactly the same size. One with compression
# and one without. Ensure the compressed file is smaller.
#
# STRATEGY:
# Use "zfs set" to turn on compression and create files before
# and after the set call. The compressed file should be smaller.
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

typeset OP=create

log_assert "Ensure that compressed files are smaller."

log_note "Ensure compression is off"
log_must $ZFS set compression=off $TESTPOOL/$TESTFS

log_note "Writing file without compression..."
log_must $FILE_WRITE -o $OP -f $TESTDIR/$TESTFILE0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

log_note "Add compression property to the dataset and write another file"
log_must $ZFS set compression=on $TESTPOOL/$TESTFS

log_must $FILE_WRITE -o $OP -f $TESTDIR/$TESTFILE1 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

log_must $SYNC $TESTDIR

FILE0_BLKS=`$DU -k $TESTDIR/$TESTFILE0 | $AWK '{ print $1}'`
FILE1_BLKS=`$DU -k $TESTDIR/$TESTFILE1 | $AWK '{ print $1}'`

if [[ $FILE0_BLKS -le $FILE1_BLKS ]]; then
	log_fail "$TESTFILE0 is smaller than $TESTFILE1" \
			"($FILE0_BLKS <= $FILE1_BLKS)"
fi

log_pass "$TESTFILE0 is bigger than $TESTFILE1 ($FILE0_BLKS > $FILE1_BLKS)"
