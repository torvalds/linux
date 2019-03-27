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
# Copyright 2013 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)hotspare_replace_007_pos.ksh	1.0	12/08/10 SL"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib
. $STF_SUITE/tests/zfsd/zfsd.kshlib
. $STF_SUITE/include/libgnop.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfsd_autoreplace_001_neg
#
# DESCRIPTION: 
#	In a pool without the autoreplace property unset, a vdev will not be
#	replaced by physical path
#
# STRATEGY:
#	1. Create 1 storage pool without hot spares
#	2. Remove a vdev
#	4. Create a new vdev with the same physical path as the first one
#	9. Verify that it does not get added to the pool.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2013-02-4)
#
# __stc_assertion_end
#
###############################################################################

log_assert "A pool without the autoreplace property set will not replace disks by physical path"

function verify_assertion
{
	# 9. Verify that it does not get added to the pool
	for ((timeout=0; timeout<4; timeout=$timeout+1)); do
		log_mustnot check_state $TESTPOOL $REMOVAL_DISK "ONLINE"
		$SLEEP 5
	done
}

typeset PHYSPATH="some_physical_path"
typeset REMOVAL_DISK=$DISK0
typeset REMOVAL_NOP=${DISK0}.nop
typeset NEW_DISK=$DISK4
typeset NEW_NOP=${DISK4}.nop
typeset OTHER_DISKS="${DISK1} ${DISK2} ${DISK3}"
typeset ALLDISKS="${DISK0} ${DISK1} ${DISK2} ${DISK3}"
typeset ALLNOPS=${ALLDISKS//~(E)([[:space:]]+|$)/.nop\1}
set -A MY_KEYWORDS "mirror" "raidz1" "raidz2"
ensure_zfsd_running
log_must create_gnops $OTHER_DISKS
for keyword in "${MY_KEYWORDS[@]}" ; do
	log_must create_gnop $REMOVAL_DISK $PHYSPATH
	log_must create_pool $TESTPOOL $keyword $ALLNOPS
	log_must $ZPOOL set autoreplace=on $TESTPOOL

	log_must destroy_gnop $REMOVAL_DISK
	log_must create_gnop $NEW_DISK $PHYSPATH
	verify_assertion
	destroy_pool "$TESTPOOL"
	log_must destroy_gnop $NEW_DISK
done

log_pass
