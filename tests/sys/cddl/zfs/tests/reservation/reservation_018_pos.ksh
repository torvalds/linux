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
# ident	"@(#)reservation_018_pos.ksh	1.4	09/01/12 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/reservation/reservation.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: reservation_018_pos
#
# DESCRIPTION:
#
# Verify that reservation doesn't inherit its value from parent.
#
# STRATEGY:
# 1) Create a filesystem tree 
# 2) Set reservation for parents
# 3) Verify that the 'reservation' for descendent doesnot inherit the value.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-17)
#
# __stc_assertion_end
#
###############################################################################  

verify_runnable "both"

log_assert "Verify that reservation doesnot inherit its value from parent."

fs=$TESTPOOL/$TESTFS
fs_child=$TESTPOOL/$TESTFS/$TESTFS

space_avail=$(get_prop available $fs)
reserv_val=$(get_prop reservation $fs)
typeset -l reservsize=$space_avail
((reservsize = reservsize / 2 ))
log_must $ZFS set reservation=$reservsize $fs 

log_must $ZFS create $fs_child
rsv_space=$(get_prop reservation $fs_child)
[[ $rsv_space == $reservsize ]] && \
	log_fail "The reservation of child dataset inherits its value from parent."

log_pass "reservation doesnot inherit its value from parent as expected."
