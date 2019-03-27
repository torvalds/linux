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
# ident	"@(#)hotplug_001_pos.ksh	1.2	08/02/27 SMI"
#

. $STF_SUITE/tests/hotplug/hotplug.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: hotplug_001_pos
#
# DESCRIPTION:
#	When removing a device from a redundant pool, the device's state will
#	be indicated as 'REMOVED'.
#
# STRATEGY:
#	1. Create mirror/raidz/raidz2 pool.
#	2. Synchronise with device in the background.
#	3. Remove one of device of pool.
#	4. Detect removed devices status is 'REMOVED'.
#	5. Detect no FMA faulty message.
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

log_assert "When removing a device from a redundant pool, the device's " \
	"state will be indicated as 'REMOVED'."

for type in "mirror" "raidz" "raidz2"; do
	log_note "Start $type testing ..."
	setup_testenv $type

	log_must destroy_gnop $DISK0
	wait_for 15 1 check_state $TESTPOOL ${DISK0}.nop 'REMOVED' 
	log_must check_state $TESTPOOL ${DISK0}.nop 'REMOVED' 

	log_must create_gnop $DISK0
	cleanup_testenv $TESTPOOL
done

log_pass "When removing a device from a redundant pool, the device's " \
	"state will be indicated as 'REMOVED'."
