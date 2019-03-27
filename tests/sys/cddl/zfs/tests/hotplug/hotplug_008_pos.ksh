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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)hotplug_008_pos.ksh	1.3	08/02/27 SMI"
#

. $STF_SUITE/tests/hotplug/hotplug.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: hotplug_008_pos
#
# DESCRIPTION:
#	After hot spare device is revoved, the devices state will be 'REMOVED'. 
#	No FMA faults was generated.
#
# STRATEGY:
#	1. Create mirror/raidz/raidz2 pool with hot spare device.
#	2. Synchronise with device in the background.
#	3. Remove the hotspare device.
#	4. Verify the device's status is 'REMOVED'.
#	5. Verify no FMA fault was generated.
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

log_assert "When removing hotspare device, verify device status is 'REMOVED'."

for type in "mirror" "raidz" "raidz2"; do
	log_must $ZPOOL create -f $TESTPOOL $type $DISK0.nop $DISK1.nop $DISK2.nop spare $DISK3.nop

	log_must destroy_gnop $DISK3
	wait_for 15 1 check_state $TESTPOOL $DISK3.nop 'REMOVED'
	log_must check_state $TESTPOOL $DISK3.nop 'REMOVED'

	log_must create_gnop $DISK3
	cleanup_testenv $TESTPOOL
done

log_pass "When removing hotspare device, verify device status is 'REMOVED'."
