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
# ident	"@(#)hotspare_add_002_pos.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_add_002_pos
#
# DESCRIPTION: 
# 	'zpool add <pool> spare <vdev> ...' can successfully add the specified 
# 	devices to the available list of the given pool while 
#	there has activated hotspare already. 
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add hot spare devices to the pool
#	3. Activate some of the hot spares.
#	3. Verify the following devices could add to the spare list 
#		of the given pool successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2006-06-07)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
}

function verify_assertion # dev
{
	typeset dev=$1

	log_must check_hotspare_state "$TESTPOOL" "$dev" "AVAIL"
	log_must $ZPOOL replace $TESTPOOL ${pooldevs[0]} $dev
	
	cleanup_devices $ndev

	log_must $ZPOOL add "$TESTPOOL" spare $ndev
	log_must $ZPOOL remove "$TESTPOOL" $ndev

	cleanup_devices $ndev

	log_must $ZPOOL add -f "$TESTPOOL" spare $ndev
	log_must $ZPOOL remove "$TESTPOOL" $ndev

	log_must $ZPOOL detach $TESTPOOL $dev
}

log_assert "'zpool add <pool> spare <vdev> ...' can add devices to the pool while it has spare-in device." 

log_onexit cleanup

set_devs
typeset ndev=${devarray[2]}

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword"

	iterate_over_hotspares verify_assertion

	destroy_pool "$TESTPOOL"
done

log_pass "'zpool add <pool> spare <vdev> ...' executes successfully while it has spare-in device"
