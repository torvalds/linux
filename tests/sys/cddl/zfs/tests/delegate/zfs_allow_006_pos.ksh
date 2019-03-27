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
# ident	"@(#)zfs_allow_006_pos.ksh	1.2	08/11/03 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_006_pos
#
# DESCRIPTION:
#	Changing permissions in a set will change what is allowed wherever the
#	set is used.
#
# STRATEGY:
#	1. Set create as set @basic.
#	2. Allow set @basic to $STAFF1 on $ROOT_TESTFS or $ROOT_TESTVOL
#	3. Verify $STAFF1 has create permissions.
#	4. Reset snapshot,allow to $basic
#	5. Verify now $STAFF1 have create,allow,destroy permissions.
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

log_assert "Changing permissions in a set will change what is allowed " \
	"wherever the set is used."
log_onexit restore_root_datasets

fs1=$ROOT_TESTFS/fs1; fs2=$ROOT_TESTFS/fs2
log_must $ZFS create $fs1
log_must $ZFS create $fs2

eval set -A dataset $DATASETS
enc=$(get_prop encryption $dataset)
if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
	perms1="snapshot,checksum,reservation"
else
	perms1="snapshot,reservation"
fi

for dtst in $DATASETS $fs1 $fs2; do
	log_must $ZFS allow -s @basic $perms1 $dtst
	log_must $ZFS allow $STAFF1 @basic $dtst
	log_must verify_perm $dtst $perms1 $STAFF1
done

perms2="allow,send,compression,userprop"
for dtst in $DATASETS $fs1 $fs2; do
	log_must $ZFS allow -s @basic $perms2 $dtst
	log_must verify_perm $dtst ${perms1},${perms2} $STAFF1
done

log_pass "Changing permissions in a set will change what is allowed passed."
