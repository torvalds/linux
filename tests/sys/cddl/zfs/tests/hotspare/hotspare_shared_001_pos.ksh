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
# Copyright 2012 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)hotspare_shared_001_pos.ksh	1.0	08/06/12 SL"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_shared_001_pos
#
# DESCRIPTION:
# It is possible to add the same vdev to multiple pools as a shared spare
# even when that vdev is a disk instead of a file
#
# STRATEGY:
# 1. Create various combinations of two pools
# 2. 'zpool add' a hotspare disk to each of them
# 3. verify that the addition worked
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2012-08-06)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL
	poolexists $TESTPOOL1 && \
		destroy_pool $TESTPOOL1

	partition_cleanup
}

function verify_assertion # dev
{
	typeset dev=$1

	log_must $ZPOOL add $TESTPOOL1 spare $dev

	log_must check_hotspare_state "$TESTPOOL" "$dev" "AVAIL"
	log_must check_hotspare_state "$TESTPOOL1" "$dev" "AVAIL"
}


log_assert "'zpool add <pool> spare <vdev> ...' can add a disk as a shared spare to multiple pools." 

log_onexit cleanup

set_devs
typeset sdev=$DISK0
typeset pool1devs="$DISK1 $DISK2 $DISK3 $DISK4"

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword" $sdev
	log_must create_pool $TESTPOOL1 $keyword $pool1devs
	iterate_over_hotspares verify_assertion $sdev

	destroy_pool "$TESTPOOL"
	destroy_pool "$TESTPOOL1"
done
