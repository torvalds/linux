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
# ident	"@(#)zfs_share_009_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_share_009_neg
#
# DESCRIPTION:
# Verify that zfs share should fail when sharing a shared zfs filesystem 
#
# STRATEGY:
# 1. Make a zfs filesystem shared
# 2. Use zfs share to share the filesystem
# 3. Verify that zfs share returns error
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-9)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset val

	val=$(get_prop sharenfs $fs)
	if [[ $val == on ]]; then
		log_must $ZFS set sharenfs=off $fs
	fi
}

log_assert "zfs share fails with shared filesystem"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
sharenfs_val=$(get_prop sharenfs $fs)
mpt=$(get_prop mountpoint $fs)
if [[ $sharenfs_val == off ]]; then
	log_must $ZFS set sharenfs=on $fs
fi

$SHARE | $GREP $mpt >/dev/null 2>&1 
if (( $? != 0 )); then
	log_must $ZFS share $fs
fi

log_mustnot $ZFS share $fs

log_pass "zfs share fails with shared filesystem as expected."
