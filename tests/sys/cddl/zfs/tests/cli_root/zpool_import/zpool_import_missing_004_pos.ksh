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
# Copyright 2013 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_missing_004_pos
#
# DESCRIPTION:
# 	Once a pool has been exported and one or more devices are missing
# 	"zpool import" with no pool argument should exit with error code 0.
#
# STRATEGY:
#	1. Create test pool upon device files using the various combinations.
#		- Striped pool
#		- Mirror
#		- Raidz
#	2. Export the test pool.
#	3. Remove one or more devices
#	4. Verify 'zpool import' will handle missing devices successfully.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2013-07-02)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

set -A vdevs "mirror" "raidz" ""

function cleanup
{
	destroy_pool $TESTPOOL1
	log_must $RM -rf $DEVICE_DIR/*
}

function recreate_files
{
	cleanup
	typeset -i i=0
	for (( ; $i < $GROUP_NUM; i += 1 )); do
		log_must create_vdevs ${DEVICE_DIR}/${DEVICE_FILE}$i
	done
	log_must $SYNC
}

log_onexit cleanup

log_assert "Verify that zpool import succeeds when devices are missing"

typeset rootvdev
typeset option
log_must $MKDIR -p $DEVICE_DIR
for rootvdev in "${vdevs[@]}"; do
	recreate_files
	poolexists $TESTPOOL1 || \
		create_pool $TESTPOOL1 "${rootvdev}" $DEVICE_FILES

	# Remove all devices but the last, one at a time
	for device in ${DEVICE_FILES% *} ; do
		poolexists $TESTPOOL1 && log_must $ZPOOL export $TESTPOOL1
		log_must $RM -f $device
		log_must $ZPOOL import -d $DEVICE_DIR 
	done
done

log_pass "zpool import succeeded when devices were missing"
