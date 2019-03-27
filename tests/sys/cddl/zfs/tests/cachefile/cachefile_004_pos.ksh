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
# ident	"@(#)cachefile_004_pos.ksh	1.2	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cachefile/cachefile.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: cachefile_004_pos
#
# DESCRIPTION:
#	Verify set, export and destroy when cachefile is set on pool.
#
# STRATEGY:
#	1. Create two pools with one same cahcefile1.
#	2. Set cachefile of the two pools to another same cachefile2.
#	3. Verify cachefile1 not exist.
#	4. Export the two pools.
#	5. Verify cachefile2 not exist.
#	6. Import the two pools and set cachefile to cachefile2.
#	7. Destroy the two pools.
#	8. Verify cachefile2 not exist.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-24)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	destroy_pool $TESTPOOL2

	mntpnt=$(get_prop mountpoint $TESTPOOL)
	typeset -i i=0
	while ((i < 2)); do
		if [[ -e $mntpnt/vdev$i ]]; then 
			log_must $RM -f $mntpnt/vdev$i
		fi
		((i += 1))
	done

	destroy_pool $TESTPOOL

	for file in $CPATH1 $CPATH2 ; do
		if [[ -f $file ]] ; then
			log_must $RM $file
		fi
	done
}


log_assert "Verify set, export and destroy when cachefile is set on pool."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL $DISKS

mntpnt=$(get_prop mountpoint $TESTPOOL)
typeset -i i=0
while ((i < 2)); do
	log_must create_vdevs $mntpnt/vdev$i
	eval vdev$i=$mntpnt/vdev$i
	((i += 1))
done

log_must $ZPOOL create -o cachefile=$CPATH1 $TESTPOOL1 $vdev0
log_must pool_in_cache $TESTPOOL1 $CPATH1
log_must $ZPOOL create -o cachefile=$CPATH1 $TESTPOOL2 $vdev1
log_must pool_in_cache $TESTPOOL2 $CPATH1

log_must $ZPOOL set cachefile=$CPATH2 $TESTPOOL1
log_must pool_in_cache $TESTPOOL1 $CPATH2
log_must $ZPOOL set cachefile=$CPATH2 $TESTPOOL2
log_must pool_in_cache $TESTPOOL2 $CPATH2
if [[ -f $CPATH1 ]]; then
	log_fail "Verify set when cachefile is set on pool."
fi

log_must $ZPOOL export $TESTPOOL1
log_must $ZPOOL export $TESTPOOL2
if [[ -f $CPATH2 ]]; then
	log_fail "Verify export when cachefile is set on pool."
fi

log_must $ZPOOL import -d $mntpnt $TESTPOOL1
log_must $ZPOOL set cachefile=$CPATH2 $TESTPOOL1
log_must pool_in_cache $TESTPOOL1 $CPATH2
log_must $ZPOOL import -d $mntpnt $TESTPOOL2
log_must $ZPOOL set cachefile=$CPATH2 $TESTPOOL2
log_must pool_in_cache $TESTPOOL2 $CPATH2

log_must $ZPOOL destroy $TESTPOOL1
log_must $ZPOOL destroy $TESTPOOL2
if [[ -f $CPATH2 ]]; then
	log_fail "Verify destroy when cachefile is set on pool."
fi

log_pass "Verify set, export and destroy when cachefile is set on pool."

