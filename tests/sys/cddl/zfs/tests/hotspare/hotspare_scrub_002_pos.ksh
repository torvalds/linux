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
# Copyright 2013 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_scrub_002_pos
#
# DESCRIPTION: 
#   'zpool scrub will scan spares as well as original devices'
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add hot spare devices to the pool
#       4. Replace one of the original devices with a spare
#       5. Simulate errors on the spare
#       6. Scrub the pool
#       7. Verify that scrub detected the simulated errors
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2013-01-14)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	partition_cleanup
}

# Returns the number of checksums errors detected on the given vdev
function get_cksum #pool, vdev
{
	typeset pool=$1
	typeset vdev=$2
	$ZPOOL status $pool | awk -v vdev=$vdev '$1~vdev {print $5; exit}'
}

function verify_assertion # odev
{
	typeset odev=$1

	log_must $ZPOOL replace $TESTPOOL $odev $sdev
	log_must check_state $TESTPOOL "$sdev" "INUSE"

	# corrupt out the $TESTPOOL to make sdev in use
	# Skip the first input block so we don't overwrite the vdev label
	log_must $DD if=/dev/zero bs=1024k count=63 oseek=1 conv=notrunc of=$sdev

	$SYNC
	# The pool may already have started scrubbing, so don't assert this.
	# Expected postconditions are checked below anyway.
	$ZPOOL scrub $TESTPOOL
	while is_pool_scrubbing $TESTPOOL ; do
		$SLEEP 2
	done

	# Verify that scrub detected the errors
	# Some vdevs (ie raidz1) will display the errors on the spare-0 line
	# instead of on the basic vdev line
	[[ $(get_cksum $TESTPOOL $sdev) > 0 ]]
	sdev_errors=$?
	[[ $(get_cksum $TESTPOOL "spare-0") > 0 ]]
	spare0_errors=$?
	log_must [ $sdev_errors -o $spare0_errors ]

	# Now clear the old errors, remove the original device and scrub again.
	# No new errors should be found, because the scrub should've found and
	# fixed all errors
	log_must $ZPOOL clear $TESTPOOL
	log_must $ZPOOL detach $TESTPOOL $odev
	$ZPOOL scrub $TESTPOOL
	while is_pool_scrubbing $TESTPOOL ; do
		$SLEEP 2
	done
	if [ $(get_cksum $TESTPOOL $sdev) -ne 0 ]; then
		log_fail "ERROR: Scrub missed cksum errors on a spare vdev"
	fi
}

log_assert "'zpool scrub' scans spare vdevs"

log_onexit cleanup

set_devs
typeset odev="${devarray[3]}"
typeset sdev="${devarray[0]}"

# Don't test striped pools because they can't have spares
set -A keywords "mirror" "raidz" "raidz2"
for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword"

	iterate_over_hotspares verify_assertion $odev

	destroy_pool "$TESTPOOL"
done

log_pass "'zpool scrub scans spare vdevs'"
