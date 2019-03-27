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
# Copyright 2014 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib
. $STF_SUITE/tests/zfsd/zfsd.kshlib
. $STF_SUITE/include/libgnop.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfsd_autoreplace_003_pos
#
# DESCRIPTION: 
#	In a pool with the autoreplace property set, a vdev will be
#	replaced by physical path even if a spare is already active for that
#	vdev
#
# STRATEGY:
#	1. Create 1 storage pool with a hot spare
#	2. Remove a vdev
#	3. Wait for the hotspare to fully resilver
#	4. Create a new vdev with the same physical path as the first one
#	10. Verify that it does get added to the pool.
#	11. Verify that the hotspare gets removed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2013-05-13)
#
# __stc_assertion_end
#
###############################################################################

log_assert "A pool with the autoreplace property will replace disks by physical path, even if a spare is active"

function verify_assertion
{
	# Verify that the replacement disk gets added to the pool
	wait_for_pool_dev_state_change 20 $NEW_DISK ONLINE

	# Wait for resilvering to complete
	wait_until_resilvered

	# Check that the spare is deactivated
	wait_for_pool_dev_state_change 20 "$SPARE_DISK" "AVAIL"
}


typeset PHYSPATH="some_physical_path"
typeset REMOVAL_DISK=$DISK0
typeset REMOVAL_NOP=${DISK0}.nop
typeset NEW_DISK=$DISK4
typeset NEW_NOP=${DISK4}.nop
typeset SPARE_DISK=${DISK5}
typeset SPARE_NOP=${DISK5}.nop
typeset OTHER_DISKS="${DISK1} ${DISK2} ${DISK3}"
typeset OTHER_NOPS=${OTHER_DISKS//~(E)([[:space:]]+|$)/.nop\1}
set -A MY_KEYWORDS "mirror" "raidz1" "raidz2"
ensure_zfsd_running
log_must create_gnops $OTHER_DISKS $SPARE_DISK
for keyword in "${MY_KEYWORDS[@]}" ; do
	log_must create_gnop $REMOVAL_DISK $PHYSPATH
	log_must create_pool $TESTPOOL $keyword $REMOVAL_NOP $OTHER_NOPS spare $SPARE_NOP
	log_must $ZPOOL set autoreplace=on $TESTPOOL

	log_must destroy_gnop $REMOVAL_DISK
	log_must create_gnop $NEW_DISK $PHYSPATH
	verify_assertion
	destroy_pool "$TESTPOOL"
	log_must destroy_gnop $NEW_DISK
done

log_pass
