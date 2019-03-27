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
# ident	"@(#)zpool_remove_003_pos.ksh	1.2	08/11/03 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_remove_003_pos
#
# DESCRIPTION:
# Verify that 'zpool can remove hotspare devices from pool when it state
#              switch from active to inactive'
#
# STRATEGY:
# 1. Create a hotspare pool
# 2. Try to replace the inactive hotspare device to active device in the pool
# 3. Try to detach active (spare) device to make it inactive
# 3. Verify that the zpool remove succeed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-18)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	restart_zfsd

	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi       	
}

# Stop ZFSD because it interferes with our manually activated spares
stop_zfsd

log_onexit cleanup
typeset disk=${DISK}

typeset spare_devs1="${disk}p1"
typeset spare_devs2="${disk}p2"
typeset spare_devs3="${disk}p3"
typeset spare_devs4="${disk}p4"

log_assert "zpool remove can remove hotspare device which state go though" \
	" active to inactive in pool"

log_note "Check spare device which state go through active to inactive" 
log_must $ZPOOL create $TESTPOOL $spare_devs1 $spare_devs2 spare \
                 $spare_devs3 $spare_devs4
log_must $ZPOOL replace $TESTPOOL $spare_devs2 $spare_devs3
log_mustnot $ZPOOL remove $TESTPOOL $spare_devs3
log_must $ZPOOL detach $TESTPOOL $spare_devs3
log_must $ZPOOL remove $TESTPOOL $spare_devs3

log_pass "'zpool remove device passed as expected.'"
