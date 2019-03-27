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
# ident	"@(#)userquota_011_pos.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_011_pos
#
# DESCRIPTION:
#       the userquota and groupquota will not change during zfs actions, such as
#	snapshot,clone,rename,upgrade,send,receive.
#
#
# STRATEGY:
#       1. Create a pool, and create fs with preset user,group quota
#       2. Check set user|group quota via zfs snapshot|clone|list -o
#       3. Check the user|group quota can not change during zfs rename|upgrade|promote
#       4. Check the user|group quota can not change during zfs clone
#       5. Check the user|group quota can not change during zfs send/receive
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
	for ds in $TESTPOOL/fs $TESTPOOL/fs-rename $TESTPOOL/fs-clone; do
		if datasetexists $ds; then
			log_must $ZFS destroy -rRf $ds
		fi
	done
}

log_onexit cleanup

log_assert \
	"the userquota and groupquota can't change during zfs actions"

cleanup

log_must $ZFS create -o userquota@$QUSER1=$UQUOTA_SIZE \
	-o groupquota@$QGROUP=$GQUOTA_SIZE $TESTPOOL/fs

log_must $ZFS snapshot $TESTPOOL/fs@snap
log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL/fs@snap "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL/fs@snap "$GQUOTA_SIZE"


log_note "clone fs gets its parent's userquota/groupquota initially"
log_must $ZFS clone  -o userquota@$QUSER1=$UQUOTA_SIZE \
		-o groupquota@$QGROUP=$GQUOTA_SIZE \
		$TESTPOOL/fs@snap $TESTPOOL/fs-clone

log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL/fs-clone "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL/fs-clone "$GQUOTA_SIZE"

log_must eval "$ZFS list -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL/fs-clone >/dev/null 2>&1"

log_note "zfs promote can not change the previously set user|group quota"
log_must $ZFS promote $TESTPOOL/fs-clone

log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL/fs-clone "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL/fs-clone "$GQUOTA_SIZE"

log_note "zfs send receive can not change the previously set user|group quota"
log_must $ZFS send $TESTPOOL/fs-clone@snap | $ZFS receive $TESTPOOL/fs-rev

log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL/fs-rev "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL/fs-rev "$GQUOTA_SIZE"

log_note "zfs rename can not change the previously set user|group quota"
log_must $ZFS rename $TESTPOOL/fs-rev $TESTPOOL/fs-rename

log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL  >/dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL/fs-rename "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL/fs-rename "$GQUOTA_SIZE"

log_note "zfs upgrade can not change the previously set user|group quota"
log_must $ZFS upgrade $TESTPOOL/fs-rename

log_must eval "$ZFS list -r -o userquota@$QUSER1,groupquota@$QGROUP \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "userquota@$QUSER1" $TESTPOOL/fs-rename "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $TESTPOOL/fs-rename "$GQUOTA_SIZE"

log_pass \
	"the userquota and groupquota can't change during zfs actions"
