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
# ident	"@(#)zpool_import_005_pos.ksh	1.3	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_005_pos
#
# DESCRIPTION:
#	Destroyed pools devices was renamed, it still can be imported correctly.
#
# STRATEGY:
#	1. Create test pool A with several devices.
#	2. Destroy pool A and rename devices name.
#	3. Verify 'zpool import -D' succeed.
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
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must create_vdevs ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

log_assert "Destroyed pools devices was renamed, it still can be imported " \
	"correctly."
log_onexit cleanup

function perform_test
{
	typeset target=$1

	assert_pool_in_cachefile $TESTPOOL1
	log_must $ZPOOL destroy $TESTPOOL1

	log_note "Testing some devices renamed in the same directory."
	log_must $MV $VDEV0 $DEVICE_DIR/vdev0-new
	log_must $ZPOOL import -d $DEVICE_DIR -D -f $target
	log_must $ZPOOL destroy -f $TESTPOOL1

	log_note "Testing all devices moved to different directories."
	log_must $MKDIR -p $DEVICE_DIR/newdir1 $DEVICE_DIR/newdir2
	log_must $MV $VDEV1 $DEVICE_DIR/newdir1/vdev1-new
	log_must $MV $VDEV2 $DEVICE_DIR/newdir2/vdev2-new
	log_must $ZPOOL import -d $DEVICE_DIR/newdir1 -d $DEVICE_DIR/newdir2 \
		-d $DEVICE_DIR -D -f $target
	log_must $ZPOOL destroy -f $TESTPOOL1

	# Restore the vdevs to their old location so this can be re-run
	log_note "Restoring vdev files for any further runs."
	log_must $MV $DEVICE_DIR/vdev0-new $VDEV0
	log_must $MV $DEVICE_DIR/newdir1/vdev1-new $VDEV1
	log_must $MV $DEVICE_DIR/newdir2/vdev2-new $VDEV2
}

log_note "Testing import by name."
log_must $ZPOOL create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
perform_test $TESTPOOL1

log_note "Testing import by GUID."
log_must $ZPOOL create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
typeset guid=$(get_config $TESTPOOL1 pool_guid)
perform_test $guid

log_pass "Destroyed pools devices was renamed, 'zpool import -D' passed."
