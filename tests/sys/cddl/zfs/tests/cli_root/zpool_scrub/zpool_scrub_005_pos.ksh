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
# ident	"@(#)zpool_scrub_005_pos.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_scrub_005_pos
#
# DESCRIPTION:
#	When scrubbing, detach device should not break system.
#
# STRATEGY:
#	1. Setup filesys with data.
#	2. Detaching and attaching the device when scrubbing.
#	3. Try it twice, verify both of them work fine.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "When scrubbing, detach device should not break system."

log_must $ZPOOL scrub $TESTPOOL
log_must $ZPOOL detach $TESTPOOL $DISK2
log_must $ZPOOL attach $TESTPOOL $DISK1 $DISK2

while true ; do
	if is_pool_resilvered $TESTPOOL ; then
		$SLEEP 3
		break
	fi
done

log_must $ZPOOL scrub $TESTPOOL
log_must $ZPOOL detach $TESTPOOL $DISK1
log_must $ZPOOL attach $TESTPOOL $DISK2 $DISK1

log_pass "When scrubbing, detach device should not break system."
