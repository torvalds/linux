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
# ident	"@(#)reservation_007_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_007_pos
#
# DESCRIPTION:
#
# Setting a reservation on dataset should have no effect on any other
# dataset at the same level in the hierarchy beyond using up available
# space in the pool.
#
# STRATEGY:
# 1) Create a filesystem
# 2) Set a reservation on the filesystem
# 3) Create another filesystem at the same level
# 4) Set a reservation on the second filesystem
# 5) Destroy both the filesystems
# 6) Verify space accounted for correctly
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

log_assert "Verify reservations on data sets doesn't affect other data sets at" \
	" same level except for consuming space from common pool"

space_avail=`get_prop available $TESTPOOL`
space_used=`get_prop used $TESTPOOL`

resv_size_set=`expr $space_avail / 3`

#
# Function which creates two datasets, sets reservations on them,
# then destroys them and ensures that space is correctly accounted
# for.
#
# Any special arguments for create are passed in via the args 
# parameter.
#
function create_resv_destroy { # args1 dataset1 args2 dataset2

	args1=$1
        dataset1=$2
        args2=$3
        dataset2=$4

	log_must $ZFS create $args1 $dataset1

	log_must $ZFS set reservation=$RESV_SIZE $dataset1

	avail_aft_dset1=`get_prop available $TESTPOOL`
	used_aft_dset1=`get_prop used $TESTPOOL`

	log_must $ZFS create $args2 $dataset2

	log_must $ZFS set reservation=$RESV_SIZE $dataset2


	# After destroying the second dataset the space used and 
	# available totals should revert back to the values they 
	# had after creating the first dataset.
	#
	log_must $ZFS destroy -f $dataset2

	avail_dest_dset2=`get_prop available $TESTPOOL`
	used_dest_dset2=`get_prop used $TESTPOOL`

	log_must within_limits $avail_aft_dset1 $avail_dest_dset2 $RESV_TOLERANCE
	log_must within_limits $used_aft_dset1 $used_dest_dset2 $RESV_TOLERANCE


	# After destroying the first dataset the space used and
	# space available totals should revert back to the values
	# they had when the pool was first created.
	log_must $ZFS destroy -f $dataset1

	avail_dest_dset1=`get_prop available $TESTPOOL`
	used_dest_dset1=`get_prop used $TESTPOOL`

	log_must within_limits $avail_dest_dset1 $space_avail $RESV_TOLERANCE
	log_must within_limits $used_dest_dset1 $space_used $RESV_TOLERANCE
}

create_resv_destroy "" $TESTPOOL/$TESTFS1 ""  $TESTPOOL/$TESTFS2
create_resv_destroy "" $TESTPOOL/$TESTFS2 "" $TESTPOOL/$TESTFS1

log_pass "Verify reservations on data sets doesn't affect other data sets at" \
	" same level except for consuming space from common pool"
