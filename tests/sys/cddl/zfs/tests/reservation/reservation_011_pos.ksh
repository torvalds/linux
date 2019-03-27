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
# ident	"@(#)reservation_011_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_011_pos
#
# DESCRIPTION:
#
# ZFS has two mechanisms dealing with space for datasets, namely
# reservations and quotas. Setting one should not affect the other,
# provided the values are legal (i.e. enough space in pool etc).
#
# STRATEGY:
# 1) Create one filesystem
# 2) Get the current quota setting 
# 3) Set a reservation 
# 4) Verify that the quota value remains unchanged
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

log_assert "Verify reservation settings do not affect quota settings"

space_avail=`get_prop available $TESTPOOL`

(( resv_size_set = (space_avail - RESV_DELTA) / 2 ))

fs_quota=`$ZFS get quota $TESTPOOL/$TESTFS`

log_must $ZFS set reservation=$resv_size_set $TESTPOOL/$TESTFS

new_fs_quota=`$ZFS get quota $TESTPOOL/$TESTFS`

if [[ $fs_quota != $new_fs_quota ]]; then
	log_fail "Quota value on $TESTFS has changed ($fs_quota != $new_fs_quota)"
fi

log_pass "Quota settings unaffected by reservation settings"
