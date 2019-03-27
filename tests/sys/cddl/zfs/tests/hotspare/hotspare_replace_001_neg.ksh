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
# ident	"@(#)hotspare_replace_001_neg.ksh	1.4	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_replace_001_neg
#
# DESCRIPTION: 
# 	'zpool replace <pool> <odev> <ndev>...' should return fail if
#		- try to replace a basic vdev that already has an activated
#			hot spare.
#		- try to replace log device.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add hot spare devices to the pool
#	3. For each scenario, try to replace the basic vdev with the given hot spares
#	4. Verify the the replace operation get failed 
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

	for odev in ${pooldevs[@]} ; do
		log_must $ZPOOL replace $TESTPOOL $odev $dev
		log_mustnot $ZPOOL replace $TESTPOOL $odev $availdev
		log_must $ZPOOL detach $TESTPOOL $dev
	done

	if [[ -n ${logdevs[@]} ]] ; then
		for odev in ${logdevs[@]} ; do
			log_mustnot $ZPOOL replace $TESTPOOL $odev $dev
		done
	fi
}

log_assert "'zpool replace <pool> <odev> <ndev>' should fail with inapplicable scenarios." 

log_onexit cleanup

set_devs

typeset dev_nonexist
typeset availdev=${devarray[2]}
dev_nonexist=${disk}sbad_slice_num

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword" "${sparedevs[@]} $availdev"

	for odev in ${pooldevs[@]} ; do
		for ndev in $dev_nonexist ; do
			log_mustnot $ZPOOL replace $TESTPOOL $odev $ndev
		done
	done

	iterate_over_hotspares verify_assertion

	destroy_pool "$TESTPOOL"
done

log_pass "'zpool replace <pool> <odev> <ndev>' fail with inapplicable scenarios."
