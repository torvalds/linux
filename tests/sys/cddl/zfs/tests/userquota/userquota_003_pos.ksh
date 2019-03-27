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
# ident	"@(#)userquota_003_pos.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_003_pos
#
# DESCRIPTION:
#       Check the basic function of set/get userquota and groupquota on fs
#
#
# STRATEGY:
#       1. Set userquota on fs and check the zfs get 
#       2. Set groupquota on fs and check the zfs get 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
###############################################################################

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "Check the basic function of set/get userquota and groupquota on fs"

log_note "Check the set|get userquota@$QUSER1 and groupquota@QGROUP"
log_must $ZFS set userquota@$QUSER1=$UQUOTA_SIZE $QFS
log_must check_quota "userquota@$QUSER1" $QFS "$UQUOTA_SIZE"

log_must $ZFS set groupquota@$QGROUP=$GQUOTA_SIZE $QFS
log_must check_quota "groupquota@$QGROUP" $QFS "$GQUOTA_SIZE"

log_pass "Check the basic function of set/get userquota on fs passed as expect"
