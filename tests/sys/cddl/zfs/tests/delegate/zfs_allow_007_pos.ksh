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
# ident	"@(#)zfs_allow_007_pos.ksh	1.3	08/11/03 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_007_pos
#
# DESCRIPTION:
#	Verify the permissions set will be masked on its descendent
#	datasets by same name set.
#
# STRATEGY:
#	1. Create $ROOT_TESTFS/childfs
#	2. Set permission $perms1 to @set on $ROOT_TESTFS
#	3. Reset permission $perms2 to @set on $ROOT_TESTFS/childfs
#	4. Allow @set to $STAFF1 on $ROOT_TESTFS/childfs
#	5. Verify $perms2 is delegated on $ROOT_TESTFS/childfs and its
#	   descendent.
#	6. Allow @set to $STAFF1 on $ROOT_TESTFS
#	7. Verify $perms1 is not appended to $STAFF1 on $ROOT_TESTFS/childfs and
#	   its descendent since it is masked
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify permission set can be masked on descendent dataset."
log_onexit restore_root_datasets

typeset perms1="snapshot,reservation,compression"
eval set -A dataset $DATASETS
enc=$(get_prop encryption $dataset)
if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
	typeset perms2="send,allow,userprop"
else
	typeset perms2="checksum,send,allow,userprop"
fi

#
# Define three level filesystems
#
childfs=$ROOT_TESTFS/childfs
grandchild=$childfs/grandchild
log_must $ZFS create $childfs
log_must $ZFS create $grandchild

#
# Setting different permissions to the same set on two level.
# But only assign the user at one level.
#
log_must $ZFS allow -s @set $perms1 $ROOT_TESTFS
log_must $ZFS allow -s @set $perms2 $childfs
log_must $ZFS allow $STAFF1 @set $childfs

#
# Verify only perms2 is valid to user on the level which he was assigned.
#
log_must verify_noperm $ROOT_TESTFS $perms1 $STAFF1
for fs in $childfs $grandchild ; do
	log_must verify_noperm $childfs $perms1 $STAFF1
	log_must verify_perm $childfs $perms2 $STAFF1
done

#
# Delegate @set to STAFF1 on ROOT_TESTFS, verify $perms1 will not be appended
# to its descendent datasets since it is masked
#
log_must $ZFS allow $STAFF1 @set $ROOT_TESTFS
log_must verify_perm $ROOT_TESTFS $perms1 $STAFF1
for fs in $childfs $grandchild ; do
	log_must verify_noperm $childfs $perms1 $STAFF1
	log_must verify_perm $childfs $perms2 $STAFF1
done

# Remove the mask, $perms1 will be allowed to its descendent datasets
log_must $ZFS unallow -s @set $childfs
for fs in $childfs $grandchild ; do
	log_must verify_noperm $childfs $perms2 $STAFF1
	log_must verify_perm $childfs $perms1 $STAFF1
done

log_pass "Verify permission set can be masked on descendetn dataset pass."
