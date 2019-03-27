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
# ident	"@(#)zpool_add_009_neg.ksh	1.4	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_add/zpool_add.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_add_009_neg
#
# DESCRIPTION: 
#       'zpool add' should return fail if vdevs are the same or vdev is 
# contained in the given pool
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add the two same devices to pool A
#	3. Add the device in pool A to pool A again
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-29)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{

        poolexists "$TESTPOOL" && \
                destroy_pool "$TESTPOOL"

	partition_cleanup

}

log_assert "'zpool add' should fail if vdevs are the same or vdev is " \
	"contained in the given pool."

log_onexit cleanup

create_pool "$TESTPOOL" "${disk}p1"
log_must poolexists "$TESTPOOL"

log_mustnot $ZPOOL add -f "$TESTPOOL" ${disk}p2 ${disk}p2
log_mustnot $ZPOOL add -f "$TESTPOOL" ${disk}p1

log_pass "'zpool add' get fail as expected if vdevs are the same or vdev is " \
	"contained in the given pool."
