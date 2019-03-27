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
# ident	"@(#)zfs_promote_007_neg.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_promote_007_neg
#
# DESCRIPTION: 
#	'zfs promote' can deal with conflicts in the namespaces.
#
# STRATEGY:
#	1. Create a snapshot and a clone of the snapshot
#	2. Create the same name snapshot for the clone
#	3. Promote the clone filesystem
#	4. Verify the promote operation fail due to the name conflicts.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-05-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	snapexists $snap && \
		log_must $ZFS destroy -rR $snap

	typeset data
	for data in $TESTDIR/$TESTFILE0 $TESTDIR/$TESTFILE1; do
		[[ -e $data ]] && $RM -f $data
	done
}

log_assert "'zfs promote' can deal with name conflicts." 
log_onexit cleanup

snap=$TESTPOOL/$TESTFS@$TESTSNAP
clone=$TESTPOOL/$TESTCLONE
clonesnap=$TESTPOOL/$TESTCLONE@$TESTSNAP

# setup for promte testing
log_must $MKFILE $FILESIZE $TESTDIR/$TESTFILE0
log_must $ZFS snapshot $snap
log_must $MKFILE $FILESIZE $TESTDIR/$TESTFILE1
log_must $RM -f $TESTDIR/$TESTFILE0
log_must $ZFS clone $snap $clone
log_must $MKFILE $FILESIZE /$clone/$CLONEFILE
log_must $ZFS snapshot $clonesnap

log_mustnot $ZFS promote $clone

log_pass "'zfs promote' deals with name conflicts as expected."

