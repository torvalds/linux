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
# ident	"@(#)zpool_create_002_pos.ksh	1.5	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_002_pos
#
# DESCRIPTION:
# 'zpool create -f <pool> <vspec> ...' can successfully create a
# new pool in some cases.
#
# STRATEGY:
# 1. Prepare the scenarios for '-f' option
# 2. Use -f to override the devices to create new pools
# 3. Verify the pool created successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	for pool in $TESTPOOL $TESTPOOL1 $TESTPOOL2 $TESTPOOL3 $TESTPOOL4 \
		$TESTPOOL5 $TESTPOOL6
	do
		poolexists $pool && destroy_pool $pool
	done

	clean_blockfile "$TESTDIR0 $TESTDIR1"

	for file in $TMPDIR/$FILEDISK0 $TMPDIR/$FILEDISK1 $TMPDIR/$FILEDISK2
	do
		if [[ -e $file ]]; then
			$RM -rf $file
		fi
	done
}

log_onexit cleanup

log_assert "'zpool create -f <pool> <vspec> ...' can successfully create" \
	"a new pool in some cases."

function create_fails_without_force
{
	log_mustnot $ZPOOL create $TESTPOOL $*
	create_pool $TESTPOOL $*
	destroy_pool $TESTPOOL
}

[ -n "$DISK" ] && disk=$DISK || disk=$DISK0

create_pool "$TESTPOOL" "${disk}p1"
log_must $ZPOOL export $TESTPOOL
log_note "'zpool create' without '-f' will fail " \
	"while device is belong to an exported pool."
create_fails_without_force "${disk}p1"

log_assert "'zpool create' mirror without '-f' will fail " \
	"when vdevs are different sizes."
VDEV_SIZE=84m
create_vdevs $TMPDIR/$FILEDISK0
unset VDEV_SIZE
log_must create_vdevs $TMPDIR/$FILEDISK1
create_fails_without_force mirror $TMPDIR/$FILEDISK0 $TMPDIR/$FILEDISK1

log_assert "'zpool create' mirror without '-f' will fail " \
	"when devices are different types."
create_vdevs $TMPDIR/$FILEDISK0
log_mustnot $ZPOOL create "$TESTPOOL4" "mirror" $TMPDIR/$FILEDISK0 ${disk}p3
create_fails_without_force mirror $TMPDIR/$FILEDISK0 ${disk}p3

log_assert "'zpool create' without '-f' will fail " \
	"while device is part of potentially active pool."
create_vdevs $TMPDIR/$FILEDISK1 $TMPDIR/$FILEDISK2
create_pool "$TESTPOOL5"  "mirror" $TMPDIR/$FILEDISK1 $TMPDIR/$FILEDISK2
log_must $ZPOOL offline $TESTPOOL5 $TMPDIR/$FILEDISK2
log_must $ZPOOL export $TESTPOOL5
create_fails_without_force $TMPDIR/$FILEDISK2

log_pass "'zpool create -f <pool> <vspec> ...' success."
