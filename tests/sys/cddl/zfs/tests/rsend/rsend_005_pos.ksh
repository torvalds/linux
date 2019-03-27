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
# ident	"@(#)rsend_005_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_005_pos
#
# DESCRIPTION:
#	zfs send -R -I send all the incremental between fs@init with fs@final
#
# STRATEGY:
#	1. Setup test model
#	2. Send -R -I @init @final on pool
#	3. Destroy all the snapshots which is later than @init
#	4. Verify receive can restore all the snapshots and data
#	5. Do the same test on filesystem and volume
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

log_assert "zfs send -R -I send all the incremental between @init with @final"
log_onexit cleanup_pool $POOL2

#
# Duplicate POOL2 for testing
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-final-R"

if is_global_zone ; then
	#
	# Testing send -R -I from pool
	#
	log_must eval "$ZFS send -R -I @init $POOL2@final > " \
		"$BACKDIR/pool-init-final-IR"
	list=$(getds_with_suffix $POOL2 @snapA)
	list="$list $(getds_with_suffix $POOL2 @snapB)"
	list="$list $(getds_with_suffix $POOL2 @snapC)"
	list="$list $(getds_with_suffix $POOL2 @final)"
	log_must destroy_tree $list
	log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-init-final-IR"
	log_must cmp_ds_cont $POOL $POOL2
fi

dstds=$(get_dst_ds $POOL $POOL2)
#
# Testing send -R -I from filesystem
#
log_must eval "$ZFS send -R -I @init $dstds/$FS@final > " \
	"$BACKDIR/fs-init-final-IR"
list=$(getds_with_suffix $dstds/$FS @snapA)
list="$list $(getds_with_suffix $dstds/$FS @snapB)"
list="$list $(getds_with_suffix $dstds/$FS @snapC)"
list="$list $(getds_with_suffix $dstds/$FS @final)"
log_must destroy_tree $list
if is_global_zone ; then
	log_must eval "$ZFS receive -d -F $dstds < $BACKDIR/fs-init-final-IR"
else
	$ZFS receive -d -F $dstds < $BACKDIR/fs-init-final-IR
fi
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

if is_global_zone ; then
	#
	# Testing send -I -R for volume
	#
	vol=$POOL2/$FS/vol
	log_must eval "$ZFS send -R -I @init $vol@final > " \
		"$BACKDIR/vol-init-final-IR"
	log_must destroy_tree $vol@snapB $vol@snapC $vol@final
	log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/vol-init-final-IR"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

log_pass "zfs send -R -I send all the incremental between @init with @final"
