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
# ident	"@(#)rsend_004_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_004_pos
#
# DESCRIPTION:
#	zfs send -R -i send incremental from fs@init to fs@final.
#
# STRATEGY:
#	1. Create a set of snapshots and fill with data.
#	2. Create sub filesystems.
#	3. Create final snapshot
#	4. Verify zfs send -R -i will backup all the datasets which has 
#	   snapshot suffix @final
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

log_assert "zfs send -R -i send incremental from fs@init to fs@final."
log_onexit cleanup_pool $POOL2

#
# Duplicate POOL2 for testing
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-final-R"

if is_global_zone ; then
	#
	# Testing send -R -i backup from pool
	#
	srclist=$(getds_with_suffix $POOL2 @final)
	interlist="$srclist $(getds_with_suffix $POOL2 @snapC)"
	interlist="$interlist $(getds_with_suffix $POOL2 @snapB)"
	interlist="$interlist $(getds_with_suffix $POOL2 @snapA)"

	log_must eval "$ZFS send -R -i @init $POOL2@final > " \
		"$BACKDIR/pool-init-final-iR"
	log_must destroy_tree $interlist
	log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-init-final-iR"

	# Get current datasets with suffix @final
	dstlist=$(getds_with_suffix $POOL2 @final)
	if [[ $srclist != $dstlist ]]; then
		log_fail "Unexpected: srclist($srclist) != dstlist($dstlist)"
	fi
	log_must cmp_ds_cont $POOL $POOL2
fi

dstds=$(get_dst_ds $POOL $POOL2)
#
# Testing send -R -i backup from filesystem
#
log_must eval "$ZFS send -R -i @init $dstds/$FS@final > " \
	"$BACKDIR/fs-init-final-iR"

srclist=$(getds_with_suffix $dstds/$FS @final)
interlist="$srclist $(getds_with_suffix $dstds/$FS @snapC)"
interlist="$interlist $(getds_with_suffix $dstds/$FS @snapB)"
interlist="$interlist $(getds_with_suffix $dstds/$FS @snapA)"
log_must destroy_tree $interlist
if is_global_zone ; then
	log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/fs-init-final-iR"
else
	$ZFS receive -F -d $dstds/$FS < $BACKDIR/fs-init-final-iR
fi

dstlist=$(getds_with_suffix $dstds/$FS @final)
if [[ $srclist != $dstlist ]]; then
	log_fail "Unexpected: srclist($srclist) != dstlist($dstlist)"
fi
log_must cmp_ds_cont $POOL $POOL2

if is_global_zone ; then
	#
	# Testing send -R -i backup from volume
	#
	srclist=$(getds_with_suffix $POOL2/$FS/vol @final)
	log_must eval "$ZFS send -R -i @init $POOL2/$FS/vol@final > " \
		"$BACKDIR/vol-init-final-iR"
	log_must destroy_tree $srclist
	log_must eval "$ZFS receive -d $POOL2 < $BACKDIR/vol-init-final-iR"

	dstlist=$(getds_with_suffix $POOL2/$FS/vol @final)
	if [[ $srclist != $dstlist ]]; then
		log_fail "Unexpected: srclist($srclist) != dstlist($dstlist)"
	fi
	log_must cmp_ds_cont $POOL $POOL2
fi

log_pass "zfs send -R -i send incremental from fs@init to fs@final."
