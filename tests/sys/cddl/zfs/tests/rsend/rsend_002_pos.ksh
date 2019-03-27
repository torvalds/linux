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
# ident	"@(#)rsend_002_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_002_pos
#
# DESCRIPTION:
#	zfs send -I sends all incrementals from fs@init to fs@final.
#
# STRATEGY:
#	1. Create several snapshots in pool2 
#	2. Send -I @snapA @final
#	3. Destroy all the snapshot except @snapA
#	4. Make sure all the snapshots and content are recovered
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "zfs send -I sends all incrementals from fs@init to fs@final."
log_onexit cleanup_pool $POOL2

#
# Duplicate POOL2
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-R"

if is_global_zone ; then
	#
	# Verify send -I will backup all the incrementals in pool
	#
	log_must eval "$ZFS send -I $POOL2@init $POOL2@final > " \
		"$BACKDIR/pool-init-final-I"
	log_must destroy_tree $POOL2@final $POOL2@snapC $POOL2@snapA 
	log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-init-final-I"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

dstds=$(get_dst_ds $POOL $POOL2)

#
# Verify send -I will backup all the incrementals in filesystem
#
log_must eval "$ZFS send -I @init $dstds/$FS@final > $BACKDIR/fs-init-final-I"
log_must destroy_tree $dstds/$FS@final $dstds/$FS@snapC $dstds/$FS@snapB
log_must eval "$ZFS receive -d -F $dstds < $BACKDIR/fs-init-final-I"
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

if is_global_zone ; then
	#
	# Verify send -I will backup all the incrementals in volume
	#
	dataset=$POOL2/$FS/vol
	log_must eval "$ZFS send -I @vsnap $dataset@final > " \
		"$BACKDIR/vol-vsnap-final-I"
	log_must destroy_tree $dataset@final $dataset@snapC  \
		$dataset@snapB $dataset@init
	log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/vol-vsnap-final-I"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

log_pass "zfs send -I sends all incrementals from fs@init to fs@final."
