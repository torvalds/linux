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
# Copyright 2013 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)hotspare_add_004_neg.ksh	1.7	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_add_004_neg
#
# DESCRIPTION: 
# 'zpool add' will not allow a swap device to be used as a hotspare
#
# STRATEGY:
#	1. Create pools
#	2. Create a swap device
#	3. Try to add [-f] the swap device to the pool
#	4. Verify the add operation failes as expected.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2013-02-15)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	poolexists "$TESTPOOL" && \
		destroy_pool "$TESTPOOL"

	if $SWAPCTL -l | grep -q $SWAPDEV; then
		log_must $SWAPOFF $SWAPDEV
	fi

	partition_cleanup
}

log_assert "'zpool add [-f]' will not allow a swap device to be used as a hotspare'"

log_onexit cleanup

set_devs

SWAPDEV="$DISK1"

if $SWAPON $SWAPDEV; then
	true
else
	log_unsupported "Cannot activate $SWAPDEV as a swap device"
fi

create_pool "$TESTPOOL" "$DISK0"
log_must poolexists "$TESTPOOL"

log_mustnot "$ZPOOL" add "$TESTPOOL" spare "$SWAPDEV"
log_mustnot "$ZPOOL" add -f "$TESTPOOL" spare "$SWAPDEV"

log_pass "'zpool add [-f]' will not allow a swap device to be used as a hotspare'"
