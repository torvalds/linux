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
# ident	"@(#)userquota_002_pos.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_002_pos
#
# DESCRIPTION:
#       the userquota and groupquota can be set during zpool or zfs creation"
#
#
# STRATEGY:
#       1. Set userquota and groupquota via "zpool -O or zfs create -o"
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
###############################################################################

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1

	if [[ -f $pool_vdev ]]; then
		$RM -f $pool_vdev
	fi
}

log_onexit cleanup

log_assert \
	"the userquota and groupquota can be set during zpool,zfs creation"

typeset pool_vdev=$TMPDIR/pool_dev.${TESTCASE_ID}

log_must create_vdevs $pool_vdev
destroy_pool $TESTPOOL1

log_must $ZPOOL create -O userquota@$QUSER1=$UQUOTA_SIZE \
	-O groupquota@$QGROUP=$GQUOTA_SIZE $TESTPOOL1 $pool_vdev

log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL1 > /dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL1 "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL1 "$GQUOTA_SIZE"

log_must $ZFS create -o userquota@$QUSER1=$UQUOTA_SIZE \
	-o groupquota@$QGROUP=$GQUOTA_SIZE $TESTPOOL1/fs

log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL1 > /dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL1/fs "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL1/fs "$GQUOTA_SIZE"

log_pass \
	"the userquota and groupquota can be set during zpool,zfs creation"
