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
# ident	"@(#)zpool_create_019_pos.ksh	1.2	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_019_pos
#
# DESCRIPTION:
#
# zpool create cannot create pools specifying readonly properties
#
# STRATEGY:
# 1. Attempt to create a pool, specifying each readonly property in turn
# 2. Verify the pool was not created
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

function cleanup
{
	if poolexists $TESTPOOL ; then
                destroy_pool $TESTPOOL
        fi
}

log_onexit cleanup

log_assert "zpool create cannot create pools specifying readonly properties"

if [[ -n $DISK ]]; then
	disk=$DISK
else
	disk=$DISK0
fi

set -A props "available" "capacity" "guid"  "health"  "size" "used"
set -A vals  "100"       "10"       "12345" "HEALTHY" "10"   "10"

typeset -i i=0;
while [ $i -lt "${#props[@]}" ]
do
        # try to set each property in the prop list with it's corresponding val
        log_mustnot $ZPOOL create -o ${props[$i]}=${vals[$i]} $TESTPOOL $disk
	if poolexists $TESTPOOL
	then
		log_fail "$TESTPOOL was created when setting ${props[$i]}!"
	fi
        i=$(( $i + 1))
done

log_pass "zpool create cannot create pools specifying readonly properties"
