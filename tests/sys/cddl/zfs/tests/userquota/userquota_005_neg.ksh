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
# ident	"@(#)userquota_005_neg.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_005_neg
#
# DESCRIPTION:
#       Check the invalid parameter of zfs set user|group quota
#
#
# STRATEGY:
#       1. check the invalid zfs set user|group quota to fs 
#       1. check the valid zfs set user|group quota to snapshots
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
###############################################################################

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

function cleanup
{
	if datasetexists $snap_fs; then
		log_must $ZFS destroy $snap_fs
	fi

	log_must cleanup_quota	
}

log_onexit cleanup

log_assert "Check the invalid parameter of zfs set user|group quota"
typeset snap_fs=$QFS@snap

log_must $ZFS snapshot $snap_fs

set -A no_users "mms1234" "ss@#" "root-122"
for user in "${no_users[@]}"; do
	log_mustnot $ID $user
	log_mustnot $ZFS set userquota@$user=100m $QFS
done

log_note "can set all numberic id even that id is not existed"
log_must $ZFS set userquota@12345678=100m $QFS
log_mustnot $ZFS set userquota@12345678=100m $snap_fs

set -A sizes "100mfsd" "m0.12m" "GGM" "-1234-m" "123m-m"

for size in "${sizes[@]}"; do
	log_note "can not set user quota with invalid size parameter"
	log_mustnot $ZFS set userquota@root=$size $QFS
done

log_note "can not set user quota to snapshot $snap_fs"
log_mustnot $ZFS set userquota@root=100m $snap_fs


set -A no_groups "aidsf@dfsd@" "123223-dsfds#sdfsd" "mss_#ss" "@@@@"
for group in "${no_groups[@]}"; do
	log_mustnot eval "$GREP $group /etc/group"
	log_mustnot $ZFS set groupquota@$group=100m $QFS
done

log_note "can not set group quota with invalid size parameter"
log_mustnot $ZFS set groupquota@root=100msfsd $QFS

log_note "can not set group quota to snapshot $snap_fs"
log_mustnot $ZFS set groupquota@root=100m $snap_fs

log_pass "Check the invalid parameter of zfs set user|group quota pas as expect"
