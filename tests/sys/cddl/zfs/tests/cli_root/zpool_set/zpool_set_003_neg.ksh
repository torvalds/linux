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
# ident	"@(#)zpool_set_003_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_set_003_neg
#
# DESCRIPTION:
#
# zpool set cannot set a readonly property
#
# STRATEGY:
# 1. Create a pool
# 2. Verify that we can't set readonly properties on that pool
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-24)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL
        $RM $TMPDIR/zpool_set_003.${TESTCASE_ID}.dat
}

set -A props "available" "capacity" "guid"  "health"  "size" "used"
set -A vals  "100"       "10"       "12345" "HEALTHY" "10"   "10"

$ZPOOL upgrade -v 2>&1 | $GREP "bootfs pool property" > /dev/null
if [ $? -ne 0 ]
then
        log_unsupported "Pool properties not supported on this release."
fi



log_onexit cleanup

log_assert "zpool set cannot set a readonly property"

VDEV=$TMPDIR/zpool_set_003.${TESTCASE_ID}.vdev
log_must create_vdevs $VDEV
log_must $ZPOOL create $TESTPOOL $VDEV

typeset -i i=0;
while [ $i -lt "${#props[@]}" ]
do
	# try to set each property in the prop list with it's corresponding val
        log_mustnot eval "$ZPOOL set ${props[$i]}=${vals[$i]} $TESTPOOL \
 > /dev/null 2>&1"
        i=$(( $i + 1))
done

log_pass "zpool set cannot set a readonly property"

