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
# ident	"@(#)hotspare_detach_002_pos.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_detach_002_pos
#
# DESCRIPTION:
#	If a hot spare have been activated,
#	and invoke "zpool detach" with the original device,
#	then the hot spare will become a functioning device,
#	and automatically be removed from the list of available hot spares
#	then the spare is automatically removed once the replace completes.
#
# STRATEGY:
#	1. Create a storage pool with hot spares
#	2. Activate a spare device to the pool
#	3. Do 'zpool detach' with the original device
#	4. Verify the spare device will become a functioning device,
#		be removed from the list of available spares as well,
#		and the original drive will removed once replace completes.
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
	typeset odev=${pooldevs[0]}

	log_must $ZPOOL replace $TESTPOOL $odev $dev
	while check_state "$TESTPOOL" "replacing" \
		"online" || \
		! is_pool_resilvered $TESTPOOL ; do 
		$SLEEP 2
	done
	log_must check_hotspare_state "$TESTPOOL" "$dev" "INUSE"

	log_must $ZPOOL detach $TESTPOOL $odev
	$SLEEP 5
	log_must $ZPOOL replace $TESTPOOL $dev $odev

	while check_state "$TESTPOOL" "replacing" \
		"online" ; do 
		$SLEEP 2
	done

	log_must $ZPOOL add -f $TESTPOOL spare $dev
	log_must check_hotspare_state "$TESTPOOL" "$dev" "AVAIL"
}

log_assert "'zpool detach <pool> <vdev> ...' against a functioning device that have spared should take the hot spare permanently swapping in successfully."

log_onexit cleanup

set_devs

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword"

	iterate_over_hotspares verify_assertion

	destroy_pool "$TESTPOOL"
done

log_pass "'zpool detach <pool> <vdev> ...' against a functioning device that have spared take the hot spare permanently swapping in successfully."
