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
# ident	"@(#)zfs_allow_005_pos.ksh	1.4	09/08/06 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_005_pos
#
# DESCRIPTION:
#	Verify option '-c' will be granted locally to the creator on any
#	newly-created descendent file systems.
#
# STRATEGY:
#	1. Allow create permissions to everyone on $ROOT_TESTFS locally.
#	2. Allow '-c' create to $ROOT_TESTFS.
#	3. chmod 777 the mountpoint of $ROOT_TESTFS
#	4. Verify only creator can create descendent dataset on 
#	   $ROOT_TESTFS/$user.
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

log_assert "Verify option '-c' will be granted locally to the creator."
log_onexit restore_root_datasets

eval set -A dataset $DATASETS
enc=$(get_prop encryption $dataset)
if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
	typeset perms="snapshot,reservation,compression,allow,\
userprop"
else
	typeset perms="snapshot,reservation,compression,checksum,\
allow,userprop"
fi

if check_version "5.10" ; then
	perms="${perms},send"
fi

log_must $ZFS allow -l everyone create,mount $ROOT_TESTFS
log_must $ZFS allow -c $perms $ROOT_TESTFS

mntpnt=$(get_prop mountpoint $ROOT_TESTFS)
log_must $CHMOD 777 $mntpnt

for user in $EVERYONE; do
	childfs=$ROOT_TESTFS/$user

	user_run $user $ZFS create $childfs

	for other in $EVERYONE; do
		#
		# Verify only the creator has the $perm time permissions.
		#
		if [[ $other == $user ]]; then
			log_must verify_perm $childfs $perms $user
		else
			log_must verify_noperm $childfs $perms $other
		fi
	done
done

log_pass "Verify option '-c' will be granted locally to the creator passed."
