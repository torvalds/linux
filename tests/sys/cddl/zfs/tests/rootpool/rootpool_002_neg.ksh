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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)rootpool_002_neg.ksh	1.2	09/05/19 SMI"
#
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  rootpool_002_neg
#
# DESCRIPTION:
#
# the zfs rootpool can not be destroyed
#
# STRATEGY:
# 1) check if the current system is installed as zfs root 
# 2) get the rootpool
# 3) try to destroy the rootpool, which should fail.
# 4) try to destroy the rootpool filesystem, which should fail.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-01-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"
log_assert "zpool/zfs destory <rootpool> should return error"


typeset rootpool=$(get_rootpool)

log_mustnot $ZPOOL destroy $rootpool

log_mustnot $ZFS destroy $rootpool

log_pass "rootpool can not be destroyed"

