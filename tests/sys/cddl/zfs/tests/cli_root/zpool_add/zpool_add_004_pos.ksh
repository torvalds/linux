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
# ident	"@(#)zpool_add_004_pos.ksh	1.6	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_add/zpool_add.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_add_004_pos
#
# DESCRIPTION: 
# 	'zpool add <pool> <vdev> ...' can successfully add a zfs volume 
# to the given pool
#
# STRATEGY:
#	1. Create a storage pool and a zfs volume
#	2. Add the volume to the pool
#	3. Verify the devices are added to the pool successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2005-09-29)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool "$TESTPOOL"

	datasetexists $TESTPOOL1/$TESTVOL && \
		log_must $ZFS destroy -f $TESTPOOL1/$TESTVOL
	poolexists $TESTPOOL1 && \
		destroy_pool "$TESTPOOL1"	

	partition_cleanup

}

log_assert "'zpool add <pool> <vdev> ...' can add zfs volume to the pool." 

log_onexit cleanup

create_pool "$TESTPOOL" "${disk}p1"
log_must poolexists "$TESTPOOL"

create_pool "$TESTPOOL1" "${disk}p2"
log_must poolexists "$TESTPOOL1"
log_must $ZFS create -V $VOLSIZE $TESTPOOL1/$TESTVOL

log_must $ZPOOL add "$TESTPOOL" /dev/zvol/$TESTPOOL1/$TESTVOL

log_must iscontained "$TESTPOOL" "/dev/zvol/$TESTPOOL1/$TESTVOL"

log_pass "'zpool add <pool> <vdev> ...' adds zfs volume to the pool successfully"
