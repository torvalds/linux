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
# ident	"@(#)refquota_003_pos.ksh	1.1	08/02/29 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refquota_003_pos
#
# DESCRIPTION:
#	Sub-filesystem quotas are not enforced by property 'refquota'
#
# STRATEGY:
#	1. Setting quota and refquota for parent. refquota < quota
#	2. Verify sub-filesystem will not be limited by refquota
#	3. Verify sub-filesystem will only be limited by quota
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-11-02)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Sub-filesystem quotas are not enforced by property 'refquota'"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
log_must $ZFS set quota=25M $fs
log_must $ZFS set refquota=10M $fs
log_must $ZFS create $fs/subfs

mntpnt=$(get_prop mountpoint $fs/subfs)
log_must $MKFILE 20M $mntpnt/$TESTFILE

typeset -i used quota refquota
used=$(get_prop used $fs)
refquota=$(get_prop refquota $fs)
((used = used / (1024 * 1024)))
((refquota = refquota / (1024 * 1024)))
if [[ $used -lt $refquota ]]; then
	log_fail "ERROR: $used < $refquota subfs quotas are limited by refquota"
fi

log_mustnot $MKFILE 20M $mntpnt/$TESTFILE.2
used=$(get_prop used $fs)
quota=$(get_prop quota $fs)
((used = used / (1024 * 1024)))
((quota = quota / (1024 * 1024)))
if [[ $used -gt $quota ]]; then
	log_fail "ERROR: $used > $quota subfs quotas aren't limited by quota"
fi

log_pass "Sub-filesystem quotas are not enforced by property 'refquota'"
