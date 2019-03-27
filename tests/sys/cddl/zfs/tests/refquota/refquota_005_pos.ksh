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
# ident	"@(#)refquota_005_pos.ksh	1.1	08/02/29 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refquota_005_pos
#
# DESCRIPTION:
#	refquotas are not limited by sub-filesystem snapshots.
#
# STRATEGY:
#	1. Setting refquota < quota for parent
#	2. Create file in sub-filesytem, take snapshot and remove the file
#	3. Verify sub-filesystem snapshot will not consume refquota 
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

log_assert "refquotas are not limited by sub-filesystem snapshots."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
log_must $ZFS set quota=25M $fs
log_must $ZFS set refquota=15M $fs
log_must $ZFS create $fs/subfs

mntpnt=$(get_prop mountpoint $fs/subfs)
typeset -i i=0
while ((i < 3)); do
	log_must $MKFILE 7M $mntpnt/$TESTFILE.$i
	log_must $ZFS snapshot $fs/subfs@snap.$i
	log_must $RM $mntpnt/$TESTFILE.$i

	((i += 1))
done

#
# Verify out of the limitation of 'quota'
#
log_mustnot $MKFILE 7M $mntpnt/$TESTFILE

log_pass "refquotas are not limited by sub-filesystem snapshots"
