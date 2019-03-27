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
# ident	"@(#)zpool_add_002_pos.ksh	1.5	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_add/zpool_add.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_add_002_pos
#
# DESCRIPTION:
# 	'zpool add -f <pool> <vdev> ...' can successfully add the specified
# devices to given pool in some cases.
#
# STRATEGY:
# 	1. Create a mirrored pool
#	2. Without -f option to add 1-way device the mirrored pool will fail
# 	3. Use -f to override the errors to add 1-way device to the mirrored 
# 	pool 
# 	4. Verify the device is added successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL && \
                destroy_pool $TESTPOOL

	partition_cleanup
}

log_assert "'zpool add -f <pool> <vdev> ...' can successfully add" \
	"devices to the pool in some cases."

log_onexit cleanup

create_pool "$TESTPOOL" mirror "${disk}p1" "${disk}p2"
log_must poolexists "$TESTPOOL"

log_mustnot $ZPOOL add "$TESTPOOL" ${disk}p3
log_mustnot iscontained "$TESTPOOL" "${disk}p3"

log_must $ZPOOL add -f "$TESTPOOL" ${disk}p3
log_must iscontained "$TESTPOOL" "${disk}p3"

log_pass "'zpool add -f <pool> <vdev> ...' executes successfully."
