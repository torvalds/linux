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
# Copyright 2017 Spectra Logic.  All rights reserved.
# Use is subject to license terms.

. $STF_SUITE/tests/hotspare/hotspare.kshlib

verify_runnable "global"

function cleanup
{
	$ZPOOL status $TESTPOOL
	if poolexists $TESTPOOL ; then 
		destroy_pool $TESTPOOL
	fi

	partition_cleanup
}

function verify_assertion # damage_type
{
	typeset mntp=$(get_prop mountpoint $TESTPOOL)

	# Write some data to the pool so the replacing vdev doesn't complete
	# immediately.
	$TIMEOUT 60s $DD if=/dev/zero of=$mntp/zerofile bs=131072

	log_must $ZINJECT -d $FAULT_DISK -A fault $TESTPOOL
	log_must check_state $TESTPOOL $FAULT_DISK FAULTED

	# Replace the failed device.  Realistically, the new device would have
	# the same physical path as the failed one, but it doesn't matter for
	# our purposes.
	log_must $ZPOOL replace $TESTPOOL $FAULT_DISK $REPLACEMENT_DISK

	# Add the spare, and check that it does not activate
	log_must $ZPOOL add $TESTPOOL spare $SDEV

	# Wait a few seconds before verifying the state
	$SLEEP 10
	log_must check_state $TESTPOOL "$SDEV" "AVAIL"
}

log_onexit cleanup

log_assert "zfsd will not use newly added spares on replacing vdevs"

ensure_zfsd_running

typeset FAULT_DISK=$DISK0
typeset REPLACEMENT_DISK=$DISK2
typeset SDEV=$DISK3
typeset POOLDEVS="$DISK0 $DISK1"
set -A MY_KEYWORDS "mirror"
for keyword in "${MY_KEYWORDS[@]}" ; do
	log_must create_pool $TESTPOOL $keyword $POOLDEVS
	verify_assertion 

	destroy_pool "$TESTPOOL"
done
