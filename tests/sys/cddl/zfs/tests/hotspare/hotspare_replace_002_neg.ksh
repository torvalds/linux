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
# ident	"@(#)hotspare_replace_002_neg.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_replace_002_neg
#
# DESCRIPTION: 
# 	'zpool replace <pool> <odev> <ndev>...' should return fail if
#	the size of hot spares is smaller than the basic vdev.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add hot spare devices to the pool
#	3. Try to replace the basic vdev with the smaller hot spares
#	4. Verify the the replace operation failes
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2006-06-07)
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

	for odev in ${pooldevs[0]} ; do
		log_mustnot $ZPOOL replace $TESTPOOL $odev $dev
	done
}

log_assert "'zpool replace <pool> <odev> <ndev>' should fail while the hot spares smaller than the basic vdev." 

log_onexit cleanup

set_devs

typeset smalldev="${devarray[6]}"
VDEV_SIZE=$SIZE1
log_must create_vdevs $smalldev
unset VDEV_SIZE

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword" "$smalldev"

	iterate_over_hotspares verify_assertion "$smalldev"

	destroy_pool "$TESTPOOL"
done

log_pass "'zpool replace <pool> <odev> <ndev>' should fail while the hot spares smaller than the basic vdev." 
