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
# ident	"@(#)reservation_006_pos.ksh	1.3	09/08/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_006_pos
#
# DESCRIPTION:
#
# Reservations (if successfully set) guarantee a minimum amount of space
# for a dataset. Unlike quotas however there should be no restrictions
# on accessing space outside of the limits of the reservation (if the
# space is available in the pool). Verify that in a filesystem with a
# reservation set that its possible to create files both within the
# reserved space and also outside.
#
# STRATEGY:
# 1) Create a filesystem
# 2) Get the space used and available in the pool
# 3) Set a reservation on the filesystem
# 4) Verify can write a file that is bigger than the reserved space
#
# i.e. we start writing within the reserved region and then continue
# for 20MB outside it.
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

log_assert "Verify can create files both inside and outside reserved areas"

space_used=`get_prop used $TESTPOOL`

log_must $ZFS set reservation=$RESV_SIZE $TESTPOOL/$TESTFS

#
# Calculate how many writes of BLOCK_SIZE it would take to fill
# up RESV_SIZE + 20971520 (20 MB). 
#
fill_size=`expr $RESV_SIZE + 20971520`
write_count=`expr $fill_size / $BLOCK_SIZE`

log_must $FILE_WRITE -o create -f $TESTDIR/$TESTFILE1 -b $BLOCK_SIZE \
	-c $write_count -d 0

log_pass "Able to create files inside and outside reserved area"
