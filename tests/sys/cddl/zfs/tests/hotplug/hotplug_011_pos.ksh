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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)hotplug_011_pos.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/hotplug/hotplug.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: hotplug_011_pos
#
# DESCRIPTION:
#	Removing device offlined, verify device status is UNAVAIL, when the 
#	system is onlined.
#
# STRATEGY:
#	1. Create mirror/raidz/raidz2 pool w/a hot spare device.
#	2. Synchronise with device in the background.
#	3. Set or unset autoreplace
#	4. Unmount all filesystems and disable syseventd and fmd.
#	5. Unload ZFS module and remove devices.
#	6. Load ZFS module and verify device the device's status is 'UNAVAIL'.
#	7. Verify no FMA fault was generated.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-01)
#
# __stc_assertion_end
#
################################################################################

log_assert "If a vdev is missing when a pool is imported, its status will be " \
	"UNAVAIL"

for type in "mirror" "raidz" "raidz2"; do
	setup_testenv $type

	log_must $ZPOOL export $TESTPOOL

	# Random remove one of devices
	log_must destroy_gnop $DISK0

	# reimport the pool
	log_must $ZPOOL import $TESTPOOL

	log_must check_state $TESTPOOL $DISK0.nop 'UNAVAIL' 

	log_must create_gnop $DISK0
	cleanup_testenv $TESTPOOL
done

log_pass "If a vdev is missing when a pool is imported, its status will be " \
	"UNAVAIL"
