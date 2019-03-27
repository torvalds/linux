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
# ident	"@(#)zfs_share_005_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_share_005_pos
#
# DESCRIPTION:
# Verify that NFS share options are propagated correctly.
#
# STRATEGY:
# 1. Create a ZFS file system.
# 2. For each option in the list, set the sharenfs property.
# 3. Verify through the share command that the options are propagated.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	log_must $ZFS set sharenfs=off $TESTPOOL/$TESTFS
	is_shared $TESTPOOL/$TESTFS && \
		log_must unshare_fs $TESTPOOL/$TESTFS
}

set -A shareopts \
    "ro" "ro=machine1" "ro=machine1:machine2" \
    "rw" "rw=machine1" "rw=machine1:machine2" \
    "ro=machine1:machine2,rw" "anon=0" "anon=0,sec=sys,rw" \
    "nosuid" "root=machine1:machine2" "rw=.mydomain.mycompany.com" \
    "rw=-terra:engineering" "log" "public"

log_assert "Verify that NFS share options are propagated correctly."
log_onexit cleanup

cleanup

typeset -i i=0
while (( i < ${#shareopts[*]} ))
do
	log_must $ZFS set sharenfs="${shareopts[i]}" $TESTPOOL/$TESTFS

	option=`get_prop sharenfs $TESTPOOL/$TESTFS`
	if [[ $option != ${shareopts[i]} ]]; then
		log_fail "get sharenfs failed. ($option != ${shareopts[i]})"
	fi

	$SHARE | $GREP $option > /dev/null 2>&1
	if (( $? != 0 )); then
		log_fail "The '$option' option was not found in share output."
	fi

	((i = i + 1))
done

log_pass "NFS options were propagated correctly."
