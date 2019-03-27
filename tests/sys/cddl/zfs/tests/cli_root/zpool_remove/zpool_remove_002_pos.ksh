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
# ident	"@(#)zpool_remove_002_pos.ksh	1.1	07/07/31 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_remove_002_pos
#
# DESCRIPTION:
# Verify that 'zpool can only remove inactive hot spare devices from pool'
#
# STRATEGY:
# 1. Create a hotspare pool
# 2. Try to remove the inactive hotspare device from the pool
# 3. Verify that the remove succeed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-18)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup
typeset disk=${DISK}

typeset spare_devs1="${disk}p1"
typeset spare_devs2="${disk}p2"

log_assert "zpool remove can only remove inactive hotspare device from pool"

log_note "check hotspare device which is created by zpool create"
log_must $ZPOOL create $TESTPOOL $spare_devs1 spare $spare_devs2
log_must $ZPOOL remove $TESTPOOL $spare_devs2

log_note "check hotspare device which is created by zpool add"
log_must $ZPOOL add $TESTPOOL spare $spare_devs2
log_must $ZPOOL remove $TESTPOOL $spare_devs2
log_must $ZPOOL destroy $TESTPOOL

log_pass "zpool remove can only remove inactive hotspare device from pool"
