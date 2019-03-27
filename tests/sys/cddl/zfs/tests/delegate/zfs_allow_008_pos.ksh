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
# ident	"@(#)zfs_allow_008_pos.ksh	1.2	07/07/31 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_008_pos
#
# DESCRIPTION:
#	non-root user can allow any permissions which he is holding to
#	other else user when it get 'allow' permission.
#
# STRATEGY:
#	1. Set two set permissions to two datasets locally.
#	2. Verify the non-root user can allow permission if he has allow
#	   permission.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify non-root user can allow permissions."
log_onexit restore_root_datasets

perms1="snapshot,reservation"
perms2="send,compression,checksum,userprop"
childfs=$ROOT_TESTFS/childfs

log_must $ZFS create $childfs

for dtst in $DATASETS ; do
	# Delegate local permission to $STAFF1
	log_must $ZFS allow -l $STAFF1 $perms1 $dtst
	log_must $ZFS allow -l $STAFF1 allow $dtst

	if [[ $dtst == $ROOT_TESTFS ]]; then
		log_must $ZFS allow -l $STAFF1 $perms2 $childfs
		# $perms1 is local permission in $ROOT_TESTFS
		log_mustnot user_run $STAFF1 $ZFS allow $OTHER1 $perms1 $childfs
		log_must verify_noperm $childfs $perms1 $OTHER1
	fi

	# Verify 'allow' give non-privilege user delegated permission.
	log_must user_run $STAFF1 $ZFS allow -l $OTHER1 $perms1 $dtst
	log_must verify_perm $dtst $perms1 $OTHER1

	# $perms2 was not allow to $STAFF1, so he have no permission to
	# delegate permission to other else.
	log_mustnot user_run $STAFF1 $ZFS allow $OTHER1 $perms2 $dtst
	log_must verify_noperm $dtst $perms2 $OTHER1
done

log_pass "Verify non-root user can allow permissions passed."
