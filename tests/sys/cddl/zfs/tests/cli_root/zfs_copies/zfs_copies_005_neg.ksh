#!/usr/local/bin/ksh93 
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
# ident	"@(#)zfs_copies_005_neg.ksh	1.3	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_copies/zfs_copies.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_copies_005_neg
#
# DESCRIPTION:
# 	Verify that copies cannot be set with pool version 1
#
# STRATEGY:
#	1. Create filesystems with copies set in a pool with version 1
#	2. Verify that the create operations fail
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-05-31)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if poolexists $ZPOOL_VERSION_1_NAME; then
		destroy_pool $ZPOOL_VERSION_1_NAME 
	fi

	if [[ -f $TESTDIR/$ZPOOL_VERSION_1_FILES ]]; then
		rm -f $TESTDIR/$ZPOOL_VERSION_1_FILES
	fi 
}

log_assert "Verify that copies cannot be set with pool version 1"
log_onexit cleanup

$CP $STF_SUITE/tests/cli_root/zpool_upgrade/blockfiles/$ZPOOL_VERSION_1_FILES $TESTDIR
$UNCOMPRESS $TESTDIR/$ZPOOL_VERSION_1_FILES
log_must $ZPOOL import -d $TESTDIR $ZPOOL_VERSION_1_NAME
log_must $ZFS create $ZPOOL_VERSION_1_NAME/$TESTFS
log_must $ZFS create -V 1m $ZPOOL_VERSION_1_NAME/$TESTVOL

for val in 3 2 1; do
	for ds in $ZPOOL_VERSION_1_NAME/$TESTFS $ZPOOL_VERSION_1_NAME/$TESTVOL; do
		log_mustnot $ZFS set copies=$val $ds
	done
	for ds in $ZPOOL_VERSION_1_NAME/$TESTFS1 $ZPOOL_VERSION_1_NAME/$TESTVOL1; do
		log_mustnot $ZFS create -o copies=$val $ds
	done
done

log_pass "Verification pass: copies cannot be set with pool version 1. "
