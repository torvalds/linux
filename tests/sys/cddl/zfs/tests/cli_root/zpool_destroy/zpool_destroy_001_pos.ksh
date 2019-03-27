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
# ident	"@(#)zpool_destroy_001_pos.ksh	1.7	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_destroy_001_pos
#
# DESCRIPTION: 
#	'zpool destroy <pool>' can successfully destroy the specified pool.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Destroy the pool
#	3. Verify the is destroyed successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL2
	destroy_pool $TESTPOOL1
	destroy_pool $TESTPOOL
	wipe_partition_table $DISK
}

set -A datasets "$TESTPOOL" "$TESTPOOL2"

log_assert "'zpool destroy <pool>' can destroy a specified pool." 

log_onexit cleanup

partition_disk $PART_SIZE $DISK 2

create_pool "$TESTPOOL" "${DISK}p1"
create_pool "$TESTPOOL1" "${DISK}p2"
log_must $ZFS create -s -V $VOLSIZE $TESTPOOL1/$TESTVOL
create_pool "$TESTPOOL2" "/dev/zvol/$TESTPOOL1/$TESTVOL"

typeset -i i=0
while (( i < ${#datasets[*]} )); do
	log_must poolexists "${datasets[i]}"
	log_must $ZPOOL destroy "${datasets[i]}"
	log_mustnot poolexists "${datasets[i]}"
	((i = i + 1))
done

log_pass "'zpool destroy <pool>' executes successfully"
