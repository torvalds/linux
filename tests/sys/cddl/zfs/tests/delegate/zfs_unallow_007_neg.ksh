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
# ident	"@(#)zfs_unallow_007_neg.ksh	1.1	07/01/09 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unallow_007_neg
#
# DESCRIPTION:
#	zfs unallow will not remove those permissions which inherited from
#	its parent filesystem.
#
# STRATEGY:
#	1. Assign perm1 to $ROOT_TESTFS
#	2. Create $SUBFS and assign perm2 to it.
#	3. Verify unallow can not affect perm1 on $SUBFS
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-30)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "zfs unallow won't remove those permissions which inherited from " \
	"its parent dataset."
log_onexit restore_root_datasets

perm1="atime,devices"; perm2="compression,checksum"
log_must $ZFS create $SUBFS
log_must $ZFS allow $STAFF1 $perm1 $ROOT_TESTFS
log_must $ZFS allow $STAFF1 $perm2 $SUBFS

log_must verify_perm $SUBFS ${perm1},${perm2} $STAFF1
#
# Athrough unallow the permissions which don't exists on the specific dataset
# return 0, the inherited permissions can't be removed in fact.
#
log_must $ZFS unallow -u $STAFF1 $perm1 $SUBFS
log_must verify_perm $SUBFS ${perm1},${perm2} $STAFF1

log_pass "Verify zfs unallow won't remove inherited permissions passed."
