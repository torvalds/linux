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
# ident	"@(#)zfs_unallow_005_pos.ksh	1.2	07/07/31 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unallow_005_pos
#
# DESCRIPTION:
#	Verify option '-c' will remove the created permission set.
#
# STRATEGY:
#	1. Set created time set to $ROOT_TESTFS.
#	2. Allow permission create to $STAFF1 on $ROOT_TESTFS.
#	3. Create $SUBFS and verify $STAFF1 have created time permissions.
#	4. Verify $STAFF1 has created time permission.
#	5. Unallow created time permission with option '-c'.
#	6. Created $SUBFS and verify $STAFF1 have not created time permissions.
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

log_assert "Verify option '-c' will remove the created permission set."
log_onexit restore_root_datasets

log_must $ZFS allow -c $LOCAL_SET $ROOT_TESTFS
log_must $ZFS allow -l $STAFF1 create,mount $ROOT_TESTFS

# Create $SUBFS and verify $SUBFS has created time permissions.
user_run $STAFF1 $ZFS create $SUBFS
if ! datasetexists $SUBFS ; then
	log_fail "ERROR: ($STAFF1): $ZFS create $SUBFS"
fi
log_must verify_perm $SUBFS $LOCAL_SET $STAFF1

#
# After unallow -c, create $SUBFS2 and verify $SUBFS2 has not created time
# permissions any more.
#
log_must $ZFS unallow -c $LOCAL_SET $ROOT_TESTFS
user_run $STAFF1 $ZFS create $SUBFS2
if ! datasetexists $SUBFS2 ; then
	log_fail "ERROR: ($STAFF1): $ZFS create $SUBFS2"
fi
log_must verify_noperm $SUBFS2 $LOCAL_SET $STAFF1

log_pass "Verify option '-c' will remove the created permission set passed."
