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
# ident	"@(#)hotspare_scrub_001_pos.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_scrub_001_pos
#
# DESCRIPTION: 
#	If a storage pool has hot spare, 
#	regardless it has been activated or NOT,
#	invoke "zpool scrub" with this storage pool should successful.
#
# STRATEGY:
#	1. Create a storage pool with hot spares
#	2. Make the storage pool dirty.
#	3. Do 'zpool scrub' with following scernarios
#		- the hotspare is only in available list
#		- the hotspare is activated
#	4. Verify the scrub runs successfully.
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
	# Record status so we can see the current state on failure.
	$ZPOOL status
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
}

function verify_assertion # dev
{
	typeset dev=$1
	typeset odev=${pooldevs[0]}

	log_must $MKFILE 100m $mtpt/$TESTFILE0
	log_must $ZPOOL scrub $TESTPOOL
	while is_pool_scrubbing $TESTPOOL ; do
		$SLEEP 2
	done

	log_must $MKFILE 100m $mtpt/$TESTFILE1
	log_must $ZPOOL replace $TESTPOOL $odev $dev

	while ! is_pool_resilvered $TESTPOOL ; do
		$SLEEP 2
	done

	log_must $ZPOOL scrub $TESTPOOL

	while is_pool_scrubbing $TESTPOOL ; do
		$SLEEP 2
	done

	log_must $ZPOOL detach $TESTPOOL $dev
	log_must $RM -f $mtpt/$TESTFILE0 \
		$mtpt/$TESTFILE1
}

log_assert "'zpool scrub <pool>' should runs successfully regardless " \
	"the hotspare is only in list or activated." 

log_onexit cleanup

typeset mtpt=""

set_devs

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword"
	mtpt=$(get_prop mountpoint $TESTPOOL)
	iterate_over_hotspares verify_assertion "${vdev%% *}"
	destroy_pool "$TESTPOOL"
done

log_pass "'zpool scrub <pool>' runs successfully regardless " \
	"the hotspare is only in list or activated." 
