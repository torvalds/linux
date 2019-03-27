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
# ident	"@(#)zpool_import_008_pos.ksh	1.3	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_008_pos
#
# DESCRIPTION:
#	For raidz2, two destroyed pool's devices were removed or used by other
#	pool, it still can be imported correctly.
#
# STRATEGY:
#	1. Create a raidz2 pool A with N disks.
#	2. Destroy this pool A.
#	3. Create another pool B with two disks which were used by pool A.
#	4. Verify import this raidz2 pool can succeed.
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
	destroy_pool $TESTPOOL2
	destroy_pool $TESTPOOL1

	log_must $RM -rf $DEVICE_DIR/*
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must create_vdevs ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

function perform_test
{
	typeset target=$1

	assert_pool_in_cachefile $TESTPOOL1
	log_must $ZPOOL destroy $TESTPOOL1

	log_must $ZPOOL create $TESTPOOL2 $VDEV0 $VDEV1
	log_must $ZPOOL import -d $DEVICE_DIR -D -f $target
	log_must $ZPOOL destroy $TESTPOOL1

	log_must $ZPOOL destroy $TESTPOOL2
	log_must $RM -rf $VDEV0 $VDEV1
	log_must $ZPOOL import -d $DEVICE_DIR -D -f $target
	log_must $ZPOOL destroy $TESTPOOL1

	log_note "For raidz2, more than two destroyed pool's devices were used, " \
		"import failed."
	log_must create_vdevs $VDEV0 $VDEV1
	log_must $ZPOOL create $TESTPOOL2 $VDEV0 $VDEV1 $VDEV2
	log_mustnot $ZPOOL import -d $DEVICE_DIR -D -f $target
	log_must $ZPOOL destroy $TESTPOOL2
}

log_assert "For raidz2, two destroyed pools devices was removed or used by " \
	"other pool, it still can be imported correctly."
log_onexit cleanup

log_note "Testing import by name."
log_must $ZPOOL create $TESTPOOL1 raidz2 $VDEV0 $VDEV1 $VDEV2 $VDEV3
perform_test $TESTPOOL1

log_note "Testing import by GUID."
log_must $ZPOOL create $TESTPOOL1 raidz2 $VDEV0 $VDEV1 $VDEV2 $VDEV3
typeset guid=$(get_config $TESTPOOL1 pool_guid)
perform_test $guid

log_pass "zpool import -D raidz2 passed."
