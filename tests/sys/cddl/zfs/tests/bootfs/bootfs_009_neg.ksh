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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)bootfs_009_neg.ksh	1.1	08/11/03 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  bootfs_009_neg
#
# DESCRIPTION:
#
# Valid encrypted datasets can't be set bootfs property values
#
# STRATEGY:
# 1. Create encrypted datasets in a test pool
# 2. Try setting encrypted datasets as boot filesystems
# 3. Verify failures.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-07-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup {
	destroy_pool $TESTPOOL
}

$ZPOOL set 2>&1 | $GREP bootfs > /dev/null
if [ $? -ne 0 ]
then
        log_unsupported "bootfs pool property not supported on this release."
fi

log_assert "Valid encrypted datasets can't be set bootfs property values"
log_onexit cleanup

DISK=${DISKS%% *}

log_must $ZPOOL create $TESTPOOL $DISK
log_must $ZFS create $TESTPOOL/$FS

enc=$(get_prop encryption $TESTPOOL/$FS)
if [ $? -ne 0 ]; then
	log_unsupported "get_prop encryption $TESTPOOL/$FS failed."
else
	if [ -z "$enc" ] || [ "$enc" = "off" ]; then
		log_unsupported "encryption isn't set to on, this test case \
is not supported."
	else
		log_mustnot $ZPOOL set bootfs=$TESTPOOL/$FS $TESTPOOL
	fi
fi


log_must $ZFS snapshot $TESTPOOL/$FS@snap
log_must $ZFS clone $TESTPOOL/$FS@snap $TESTPOOL/clone
log_must $ZFS promote $TESTPOOL/clone
log_mustnot $ZPOOL set bootfs=$TESTPOOL/clone $TESTPOOL

log_pass "Encrypted datasets can't be set bootfs property"
