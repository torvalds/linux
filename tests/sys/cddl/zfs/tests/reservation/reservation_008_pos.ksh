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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)reservation_008_pos.ksh	1.3	09/08/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_008_pos
#
# DESCRIPTION:
#
# Setting a reservation reserves a defined minimum amount of space for
# a dataset, and prevents other datasets using that space. Verify that
# reducing the reservation on a filesystem allows other datasets in
# the pool to use that space.
#
# STRATEGY:
# 1) Create multiple filesystems
# 2) Set reservations on all bar one of the filesystems
# 3) Fill up the one non-reserved filesystem
# 4) Reduce one of the reservations and verify can write more
# data into the non-reserved filesystem
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

log_assert "Verify reducing reservation allows other datasets to use space"

log_must create_multiple_fs $RESV_NUM_FS $TESTPOOL/$TESTFS $TESTDIR

space_avail=`get_prop available $TESTPOOL`
space_used=`get_prop used $TESTPOOL`

#
# To make sure this test doesn't take too long to execute on
# large pools, we calculate a reservation setting which when
# applied to all bar one of the filesystems (RESV_NUM_FS-1) will 
# ensure we have RESV_FREE_SPACE left free in the pool, which we will
# be able to quickly fill.
#
resv_space_avail=`expr $space_avail - $RESV_FREE_SPACE`
num_resv_fs=`expr $RESV_NUM_FS - 1` # Number of FS to which resv will be applied
resv_size_set=`expr $resv_space_avail / $num_resv_fs`

#
# We set the reservations now, rather than when we created the filesystems
# to allow us to take into account space used by the filsystem metadata
#
# Note we don't set a reservation on the first filesystem we created, 
# hence num=1 rather than zero below.
#
typeset -i num=1
while (( $num < $RESV_NUM_FS )); do
	log_must $ZFS set reservation=$resv_size_set $TESTPOOL/$TESTFS$num
	(( num = num + 1 ))
done

space_avail_still=`get_prop available $TESTPOOL`

fill_size=`expr $space_avail_still + $RESV_TOLERANCE`
write_count=`expr $fill_size / $BLOCK_SIZE`

# Now fill up the first filesystem (which doesn't have a reservation set
# and thus will use up whatever free space is left in the pool).
num=0
log_note "Writing to $TESTDIR$num/$TESTFILE1"

$FILE_WRITE -o create -f $TESTDIR$num/$TESTFILE1 -b $BLOCK_SIZE \
        -c $write_count -d 0
ret=$?
if (( $ret != $ENOSPC )); then
	log_fail "Did not get ENOSPC as expected (got $ret)."
fi

# Remove the reservation on one of the other filesystems and verify
# can write more data to the original non-reservation filesystem.
num=1
log_must $ZFS set reservation=none $TESTPOOL/${TESTFS}$num
num=0
log_must $FILE_WRITE -o create -f ${TESTDIR}$num/$TESTFILE2 -b $BLOCK_SIZE \
        -c 1000 -d 0

log_pass "reducing reservation allows other datasets to use space"
