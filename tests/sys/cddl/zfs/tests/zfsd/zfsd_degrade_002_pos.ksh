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
# Copyright (c) 2012-2014 Spectra Logic Corporation.  All rights reserved.
# Use is subject to license terms.
# 
# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zfsd/zfsd.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfsd_degrade_002_pos
#
# DESCRIPTION: 
#   If an active hotspare experiences checksum errors, it will become degraded.
#       
#
# STRATEGY:
#   1. Create a storage pool with a hotspare.  Only use the file vdevs because
#      it is easy to generate checksum errors on them.
#   2. fault a vdev to active the hotspare
#   3. Mostly fill the pool with data.
#   4. Corrupt it by DDing to the hotspare's underlying file.
#   5. Verify that the hotspare becomes DEGRADED.
#   6. ONLINE it and verify that it resilvers and joins the pool.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2014-05-13)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

VDEV0=${TMPDIR}/file0.${TESTCASE_ID}
VDEV1=${TMPDIR}/file1.${TESTCASE_ID}
SPARE_VDEV=${TMPDIR}/file2.${TESTCASE_ID}
BASIC_VDEVS="${VDEV0} ${VDEV1}"
VDEVS="${BASIC_VDEVS} ${SPARE_VDEV}"
TESTFILE=/$TESTPOOL/testfile
VDEV_SIZE=192m


function cleanup
{
	destroy_pool $TESTPOOL
	$RM -f $VDEVS
}

log_assert "ZFS will degrade a spare vdev that produces checksum errors"

log_onexit cleanup

ensure_zfsd_running
log_must create_vdevs $VDEV0 $VDEV1 $SPARE_VDEV

for type in "mirror" "raidz"; do
	log_note "Testing raid type $type"

	create_pool $TESTPOOL $type ${BASIC_VDEVS} spare ${SPARE_VDEV}

	# Activate the hotspare
	$ZINJECT -d ${VDEV0} -A fault $TESTPOOL

	# ZFSD can take up to 60 seconds to replace a failed device
	# (though it's usually faster).  
	wait_for_pool_dev_state_change 60 $SPARE_VDEV INUSE

	corrupt_pool_vdev $TESTPOOL $SPARE_VDEV $TESTFILE
	destroy_pool $TESTPOOL
done

cleanup
log_pass
