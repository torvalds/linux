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
# ident	"@(#)zpool_import_006_pos.ksh	1.3	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_006_pos
#
# DESCRIPTION:
#	For mirror, N-1 destroyed pools devices was removed or used by other
#	pool, it still can be imported correctly.
#
# STRATEGY:
#	1. Create mirror with N disks.
#	2. Destroy this mirror.
#	3. Create another pool with N-1 disks which was used by this mirror.
#	4. Verify import mirror can succeed.
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

log_assert "For mirror, N-1 destroyed pools devices was removed or used " \
	"by other pool, it still can be imported correctly."
log_onexit cleanup

function perform_test
{
	typeset target=$1

	assert_pool_in_cachefile $TESTPOOL1
	log_must $ZPOOL destroy $TESTPOOL1

	create_pool $TESTPOOL2 $VDEV0 $VDEV2
	log_must $ZPOOL import -d $DEVICE_DIR -D -f $target
	log_must $ZPOOL destroy $TESTPOOL1

	log_must $ZPOOL destroy $TESTPOOL2
	log_must $RM -rf $VDEV2
	log_must $ZPOOL import -d $DEVICE_DIR -D -f $target

	# Restore the vdev.
	log_must create_vdevs $VDEV2
}

log_note "Testing import by name."
create_pool $TESTPOOL1 mirror $VDEV0 $VDEV1 $VDEV2
perform_test $TESTPOOL1

log_note "Testing import by GUID."
create_pool $TESTPOOL1 mirror $VDEV0 $VDEV1 $VDEV2
typeset guid=$(get_config $TESTPOOL1 pool_guid)
perform_test $guid

log_pass "zpool import -D mirror passed."
