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
# ident	"@(#)zfs_promote_002_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_promote_002_pos
#
# DESCRIPTION: 
#	'zfs promote' can deal with multiple snapshots in the origin filesystem.
#
# STRATEGY:
#	1. Create multiple snapshots and a clone of the last snapshot
#	2. Promote the clone filesystem
#	3. Verify the promoted filesystem included all snapshots
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
	if snapexists $csnap1; then
		log_must $ZFS promote $fs
	fi

	typeset ds
	typeset data
	for ds in $snap $snap1; do
		log_must $ZFS destroy -rR $ds
	done
	for file in $TESTDIR/$TESTFILE0 $TESTDIR/$TESTFILE1; do
		[[ -e $file ]] && $RM -f $file
	done
}

log_assert "'zfs promote' can deal with multiple snapshots in a filesystem." 
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
snap=$fs@$TESTSNAP
snap1=$fs@$TESTSNAP1
clone=$TESTPOOL/$TESTCLONE
csnap=$clone@$TESTSNAP
csnap1=$clone@$TESTSNAP1

# setup for promote testing
log_must $MKFILE $FILESIZE $TESTDIR/$TESTFILE0 
log_must $ZFS snapshot $snap
log_must $MKFILE $FILESIZE $TESTDIR/$TESTFILE1
log_must $RM -f $testdir/$TESTFILE0
log_must $ZFS snapshot $snap1
log_must $ZFS clone $snap1 $clone
log_must $MKFILE $FILESIZE /$clone/$CLONEFILE

log_must $ZFS promote $clone

# verify the 'promote' operation
for ds in $csnap $csnap1; do
	! snapexists $ds && \
		log_fail "Snapshot $ds doesn't exist after zfs promote."
done
for ds in $snap $snap1; do
	snapexists $ds && \
		log_fail "Snapshot $ds is still there after zfs promote."
done

origin_prop=$(get_prop origin $fs)
[[ "$origin_prop" != "$csnap1" ]] && \
	log_fail "The dependency of $fs is not correct."
origin_prop=$(get_prop origin $clone)
[[ "$origin_prop" != "-" ]] && \
	 log_fail "The dependency of $clone is not correct."

log_pass "'zfs promote' deal with multiple snapshots as expected."

