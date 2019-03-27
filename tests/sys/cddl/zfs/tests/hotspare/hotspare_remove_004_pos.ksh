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
# ident	"@(#)hotspare_remove_004_pos.ksh	1.2	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_remove_004_pos
#
# DESCRIPTION: 
# 	'zpool remove <pool> <vdev> ...' can successfully remove the specified 
# devices from the hot spares even it no longer exists.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add hot spare devices to the pool
#	3. Export the pool
#	4. Remove the hotspare
#	5. Import the pool
#	6. Remove hot spares one by one	
#	7. Verify the devices are removed from the spare list 
#		of the given pool successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2008-02-25)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
}

function verify_assertion # dev
{
	typeset dev=$1

	log_must $ZPOOL export $TESTPOOL
	log_must $MV $dev $dev.bak
	log_must $ZPOOL import -d $HOTSPARE_TMPDIR $TESTPOOL
	log_must $ZPOOL remove $TESTPOOL $dev
	log_mustnot iscontained "$TESTPOOL" $dev
	log_must $MV $dev.bak $dev
}

log_assert "'zpool remove <pool> <vdev> ...' can remove spare device from the pool." 

log_onexit cleanup

set_devs

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword"

	iterate_over_hotspares verify_assertion

	destroy_pool "$TESTPOOL"
done

log_pass "'zpool remove <pool> <vdev> ...' executes successfully"
