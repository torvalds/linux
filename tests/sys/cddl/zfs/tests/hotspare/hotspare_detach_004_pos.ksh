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
# ident	"@(#)hotspare_detach_004_pos.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_detach_004_pos
#
# DESCRIPTION:
#	If a hot spare is activated, 
#	and invoke "zpool replace" with this hot spare to another hot spare,
#	the operation should run successfully.
#
# STRATEGY:
#	1. Create a storage pool with multiple hot spares
#	2. Activate a hot spare by 'zpool replace' with the basic dev,
#		make sure there still have enough hot spare in available list.
#	3. Do 'zpool replace' with the hot spare to another AVAIL hot spare.
#	4. Verify the operation runs successfully.
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
	if poolexists $TESTPOOL; then
		if [[ "$STF_EXITCODE" -eq "$STF_FAIL" ]]; then
			$ECHO "Testcase failed; dumping pool status:"
			$ZPOOL status $TESTPOOL
		fi
		destroy_pool $TESTPOOL
	fi

	partition_cleanup
}

function verify_assertion # dev
{
	typeset dev=$1
	typeset odev=${pooldevs[0]}

	log_must $ZPOOL replace $TESTPOOL $odev $dev
	log_must check_hotspare_state "$TESTPOOL" "$dev" "INUSE"
	wait_for_state_exit "$TESTPOOL" "$dev" "resilvering"

	log_must $ZPOOL replace $TESTPOOL $dev $ndev
	wait_for_state_exit "$TESTPOOL" "$dev" "online"

	log_must check_hotspare_state "$TESTPOOL" "$dev" "AVAIL"
	log_must check_hotspare_state "$TESTPOOL" "$ndev" "INUSE"
	log_must $ZPOOL detach $TESTPOOL $ndev
}

log_assert "'zpool replace <pool> <vdev> <ndev>' against a hot spare device that have been activated should successful while the another dev is a available hot spare."

log_onexit cleanup

set_devs
typeset ndev=${devarray[2]}

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword" "${sparedevs[@]} $ndev"

	iterate_over_hotspares verify_assertion

	destroy_pool "$TESTPOOL"
done

log_pass "'zpool replace <pool> <vdev> <ndev>' against a hot spare device that have been activated should successful while the another dev is a available hot spare."
