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
# ident	"@(#)canmount_004_pos.ksh	1.1	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: canmount_sharenfs_001_pos
#
# DESCRIPTION:
# Verify canmount=noauto work fine when setting sharenfs or sharesmb.
#
# STRATEGY:
# 1. Create a fs canmount=noauto.
# 2. Set sharenfs or sharesmb.
# 3. Verify the fs is umounted.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-05-25)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

# properties
set -A sharenfs_prop "off" "on" "rw"
set -A sharesmb_prop "off" "on"
if check_version "5.11" ; then
	set -A sharesmb_prop ${sharesmb_prop[*]} "name=anybody"
fi

function cleanup
{
	log_must $ZFS destroy -rR $CS_FS
}

function assert_unmounted
{
	mnted=$(get_prop mounted $CS_FS)
	if [[ "$mnted" == "yes" ]]; then
		canmnt=$(get_prop canmount $CS_FS)
		shnfs=$(get_prop sharenfs $CS_FS)
		shsmb=$(get_prop sharesmb $CS_FS)
		mntpt=$(get_prop mountpoint $CS_FS)
		log_fail "$CS_FS should be unmounted" \
		"[canmount=$canmnt,sharenfs=$shnfs,sharesmb=$shsmb,mountpoint=$mntpt]."
	fi
}

log_assert "Verify canmount=noauto work fine when setting sharenfs or sharesmb."
log_onexit cleanup

CS_FS=$TESTPOOL/$TESTFS/cs_fs.${TESTCASE_ID}
oldmpt=$TESTDIR/old_cs_fs.${TESTCASE_ID}
newmpt=$TESTDIR/new_cs_fs.${TESTCASE_ID}

log_must $ZFS create -o canmount=noauto -o mountpoint=$oldmpt $CS_FS
assert_unmounted

for n in ${sharenfs_prop[@]}; do
	log_must $ZFS set sharenfs="$n" $CS_FS
	assert_unmounted
	for s in ${sharesmb_prop[@]}; do
		log_must $ZFS set sharesmb="$s" $CS_FS
		assert_unmounted

		mntpt=$(get_prop mountpoint $CS_FS)
		if [[ "$mntpt" == "$oldmpt" ]]; then
			log_must $ZFS set mountpoint="$newmpt" $CS_FS
		else
			log_must $ZFS set mountpoint="$oldmpt" $CS_FS
		fi
		assert_unmounted
	done
done

log_pass "Verify canmount=noauto work fine when setting sharenfs or sharesmb."

