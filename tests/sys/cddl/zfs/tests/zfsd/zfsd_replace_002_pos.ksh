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

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2012-2018 Spectra Logic Corporation.  All rights reserved.
# Use is subject to license terms.
# 
# Portions taken from:
# ident	"@(#)replacement_001_pos.ksh	1.4	08/02/27 SMI"
#
# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/include/libgnop.kshlib

function is_pool_unavail # pool
{
	is_pool_state "$1" "UNAVAIL"
}

log_assert "zfsd will reactivate a pool after all disks are failed and reappeared"

log_unsupported "This feature has not yet been implemented in zfsd"

ensure_zfsd_running
set_disks
typeset ALLDISKS="${DISK0} ${DISK1} ${DISK2}"
typeset ALLNOPS=${ALLDISKS//~(E)([[:space:]]+|$)/.nop\1}

log_must create_gnops $ALLDISKS
for type in "raidz" "mirror"; do
	# Create a pool on the supplied disks
	create_pool $TESTPOOL $type $ALLNOPS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	# Disable all vdevs.  The pool should become UNAVAIL
	log_must destroy_gnop $DISK0
	log_must destroy_gnop $DISK1
	log_must destroy_gnop $DISK2
	wait_for 5 1 is_pool_unavail $TESTPOOL

	# Renable all vdevs.  The pool should become healthy again
	log_must create_gnop $DISK0
	log_must create_gnop $DISK1
	log_must create_gnop $DISK2

	wait_for 5 1 is_pool_healthy $TESTPOOL

	destroy_pool $TESTPOOL
	log_must $RM -rf /$TESTPOOL
done

log_pass
