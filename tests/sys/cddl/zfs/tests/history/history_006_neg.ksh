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
# ident	"@(#)history_006_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_006_neg
#
# DESCRIPTION:
# 	Verify the following zfs subcommands are not logged.
#      	    list, get, mount, unmount, share, unshare, send
#
# STRATEGY:
#	1. Create a test pool.
#	2. Separately invoke zfs list|get|mount|unmount|share|unshare|send
#	3. Verify they was not recored in pool history.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	[[ -f $EXPECT_HISTORY ]] && $RM -f $EXPECT_HISTORY
	[[ -f $REAL_HISTORY ]] && $RM -f $REAL_HISTORY
	if datasetexists $fs ; then
		log_must $ZFS destroy -rf $fs
	fi
	log_must $ZFS create $fs
}

log_assert "Verify 'zfs list|get|mount|unmount|share|unshare|send' will not " \
	"be logged."
log_onexit cleanup

# Create initial test environment
fs=$TESTPOOL/$TESTFS; snap1=$fs@snap1; snap2=$fs@snap2
log_must $ZFS set sharenfs=on $fs
log_must $ZFS snapshot $snap1
log_must $ZFS snapshot $snap2

# Save initial TESTPOOL history
log_must eval "$ZPOOL history $TESTPOOL > $EXPECT_HISTORY"

log_must $ZFS list $fs > /dev/null
log_must $ZFS get mountpoint $fs > /dev/null
log_must $ZFS unmount $fs
log_must $ZFS mount $fs
log_must $ZFS share $fs
log_must $ZFS unshare $fs
log_must $ZFS send -i $snap1 $snap2 > /dev/null

log_must eval "$ZPOOL history $TESTPOOL > $REAL_HISTORY"
log_must $DIFF $EXPECT_HISTORY $REAL_HISTORY

log_pass "Verify 'zfs list|get|mount|unmount|share|unshare|send' passed."
