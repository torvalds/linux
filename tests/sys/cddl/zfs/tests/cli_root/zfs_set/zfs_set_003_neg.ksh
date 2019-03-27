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
# ident	"@(#)zfs_set_003_neg.ksh	1.2	09/05/19 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_set_003_neg
#
# DESCRIPTION:
# 'zfs set mountpoint/sharenfs' should fail when the mountpoint is invlid 
#
# STRATEGY:
# 1. Create invalid scenarios
# 2. Run zfs set mountpoint/sharenfs with invalid value
# 3. Verify that zfs set returns expected errors
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-9)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if [ -e $badpath ]; then
		$RM -f $badpath
	fi
	if datasetexists $TESTPOOL/foo; then
		log_must $ZFS destroy $TESTPOOL/foo
	fi
}

log_assert "'zfs set mountpoint/sharenfs' fails with invalid scenarios"
log_onexit cleanup

badpath=$TMPDIR/foo1.${TESTCASE_ID}
$TOUCH $badpath
longpath=$(gen_dataset_name 1030 "abcdefg")

log_must $ZFS create -o mountpoint=legacy $TESTPOOL/foo

# Do the negative testing about "property may be set but unable to remount filesystem"
log_mustnot eval "$ZFS set mountpoint=$badpath $TESTPOOL/foo >/dev/null 2>&1"

# Do the negative testing about "property may be set but unable to reshare filesystem"
log_mustnot eval "$ZFS set sharenfs=on $TESTPOOL/foo >/dev/null 2>&1"

# Do the negative testing about "sharenfs property can not be set to null"
log_mustnot eval "$ZFS set sharenfs= $TESTPOOL/foo >/dev/null 2>&1"

# Do the too long pathname testing (>1024)
log_mustnot eval "$ZFS set mountpoint=/$longpath $TESTPOOL/foo >/dev/null 2>&1"

log_pass "'zfs set mountpoint/sharenfs' fails with invalid scenarios as expected."
