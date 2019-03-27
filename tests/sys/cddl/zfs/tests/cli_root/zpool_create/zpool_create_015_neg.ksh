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
#ident	"@(#)zpool_create_015_neg.ksh	1.2	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_015_neg
#
#
# DESCRIPTION:
# 'zpool create' will fail with zfs vol device in swap
#
#
# STRATEGY:
# 1. Create a zpool
# 2. Create a zfs vol on zpool
# 3. Add this zfs vol device to swap
# 4. Try to create a new pool with devices in swap
# 5. Verify the creation is failed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-04-17)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	# cleanup zfs pool and dataset
	if datasetexists $vol_name; then
		$SWAP -l | $GREP /dev/zvol/$vol_name > /dev/null 2>&1
		if [[ $? -eq 0 ]]; then
			$SWAP -d /dev/zvol/${vol_name}
		fi
	fi

	destroy_pool $TESTPOOL1
	destroy_pool $TESTPOOL
}

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

typeset pool_dev=${disk}p1
typeset vol_name=$TESTPOOL/$TESTVOL

log_assert "'zpool create' should fail with zfs vol device in swap."
log_onexit cleanup

#
# use zfs vol device in swap to create pool which should fail.
#
create_pool $TESTPOOL $pool_dev
log_must $ZFS create -V 100m $vol_name
log_must $SWAP -a /dev/zvol/$vol_name
for opt in "-n" "" "-f"; do
	log_mustnot $ZPOOL create $opt $TESTPOOL1 /dev/zvol/${vol_name}
done

# cleanup
log_must $SWAP -d /dev/zvol/${vol_name}
log_must $ZFS destroy $vol_name

log_pass "'zpool create' passed as expected with inapplicable scenario."
