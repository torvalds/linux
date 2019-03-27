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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_import_004_pos.ksh	1.3	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_004_pos
#
# DESCRIPTION:
#	Destroyed pools devices was moved to another directory, it still can be
#	imported correctly.
#
# STRATEGY:
#	1. Create test pool A with several devices.
#	2. Destroy pool A.
#	3. Move devices to another directory.
#	4. Verify 'zpool import -D' succeed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-12)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	log_must $RM -rf $DEVICE_DIR/*
}

function perform_test
{
	target=$1

	assert_pool_in_cachefile $TESTPOOL1
	log_must $ZPOOL destroy $TESTPOOL1

	log_note "Devices was moved to different directories."
	log_must $MKDIR -p $DEVICE_DIR/newdir1 $DEVICE_DIR/newdir2
	log_must $MV $VDEV1 $DEVICE_DIR/newdir1
	log_must $MV $VDEV2 $DEVICE_DIR/newdir2
	log_must $ZPOOL import -d $DEVICE_DIR/newdir1 -d $DEVICE_DIR/newdir2 \
		-d $DEVICE_DIR -D -f $target
	log_must $ZPOOL destroy -f $TESTPOOL1

	log_note "Devices was moved to same directory."
	log_must $MV $VDEV0 $DEVICE_DIR/newdir2
	log_must $MV $DEVICE_DIR/newdir1/* $DEVICE_DIR/newdir2
	log_must $ZPOOL import -d $DEVICE_DIR/newdir2 -D -f $target
	log_must $ZPOOL destroy -f $TESTPOOL1

	# Revert at the end so this test can be rerun.
	log_must $MV $DEVICE_DIR/newdir2/$VDEV0F $VDEV0
	log_must $MV $DEVICE_DIR/newdir2/$VDEV1F $VDEV1
	log_must $MV $DEVICE_DIR/newdir2/$VDEV2F $VDEV2
}

log_assert "Destroyed pools devices was moved to another directory," \
	"it still can be imported correctly."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
log_note "Testing import by name '$TESTPOOL1'."
perform_test $TESTPOOL1

log_must $ZPOOL create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
log_must $ZPOOL status $TESTPOOL1
log_must $ZDB -C $TESTPOOL1
typeset guid=$(get_config $TESTPOOL1 pool_guid)
log_note "Testing import by GUID '${guid}'."
perform_test $guid

log_pass "Destroyed pools devices was moved, 'zpool import -D' passed."
