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
# ident	"@(#)zfs_unmount_006_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unmount_006_pos
#
# DESCRIPTION:
#	Re-creating zfs files, 'zfs unmount' still succeed. 
#
# STRATEGY:
#	1. Create pool and filesystem.
#	2. Recreating the same file in this fs for a while, then breaking out.
#	3. Verify the filesystem can be unmount successfully.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if ! ismounted $TESTPOOL/$TESTFS ; then
		log_must $ZFS mount $TESTPOOL/$TESTFS
	fi
}

log_assert "Re-creating zfs files, 'zfs unmount' still succeed."
log_onexit cleanup

# Call cleanup to make sure the file system are mounted.
cleanup
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
(($? != 0)) && log_fail "get_prop mountpoint $TESTPOOL/$TESTFS"

typeset -i i=0
while (( i < 10000 )); do
	$CP $STF_SUITE/include/libtest.kshlib $mntpnt
	
	(( i += 1 ))
done
log_note "Recreating zfs files for 10000 times."

log_must $ZFS unmount $TESTPOOL/$TESTFS

log_pass "Re-creating zfs files, 'zfs unmount' passed."
