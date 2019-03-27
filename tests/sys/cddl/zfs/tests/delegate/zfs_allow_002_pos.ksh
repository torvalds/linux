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
# ident	"@(#)zfs_allow_002_pos.ksh	1.3	08/11/03 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_002_pos
#
# DESCRIPTION:
# <user|group> argument is interpreted as a user if possible, then as a group as
# possible.
#
# STRATEGY:
#	1. Create user $STAFF_GROUP
#	2. Delegate permissions to $STAFF_GROUP
#	3. Verify user $STAFF_GROUP has the permissions.
#	4. Delete user $STAFF_GROUP and allow the permission to $STAFF_GROUP
#	5. Verify $STAFF_GROUP is interpreted as group.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-14)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if $ID $STAFF_GROUP > /dev/null 2>&1; then
		log_must del_user $STAFF_GROUP 
	fi

	restore_root_datasets
}

log_assert "<user|group> is interpreted as user if possible, then as group."
log_onexit cleanup

eval set -A dataset $DATASETS
enc=$(get_prop encryption $dataset)
if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
	typeset perms="snapshot,reservation,compression,send,allow,\
userprop"
else
	typeset perms="snapshot,reservation,compression,checksum,\
send,allow,userprop"
fi

log_must $USERADD $STAFF_GROUP
for dtst in $DATASETS ; do
	log_must $ZFS allow $STAFF_GROUP $perms $dtst
	log_must verify_perm $dtst $perms $STAFF_GROUP
	log_must verify_noperm $dtst $perms $STAFF1 $STAFF2 
done

log_must restore_root_datasets

log_must del_user $STAFF_GROUP
for dtst in $datasets ; do
	log_must $ZFS allow $STAFF_GROUP $perms $dtst
	log_must verify_perm $dtst $perms $STAFF1 $STAFF2
done

log_pass "<user|group> is interpreted as user if possible, then as group passed."
