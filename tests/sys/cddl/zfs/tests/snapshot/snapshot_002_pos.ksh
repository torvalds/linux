#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)snapshot_002_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_002_pos
#
# DESCRIPTION:
# An archive of a zfs file system and an archive of its snapshot
# is identical even though the original file system has
# changed sinced the snapshot was taken.
#
# STRATEGY:
# 1) Create files in all of the zfs file systems
# 2) Create a tarball of the file system
# 3) Create a snapshot of the dataset
# 4) Remove all the files in the original file system
# 5) Create a tarball of the snapshot
# 6) Extract each tarball and compare directory structures
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

verify_runnable "both"

function cleanup
{
	if [[ -d $CWD ]]; then
		cd $CWD || log_fail "Could not cd $CWD"
	fi

        snapexists $SNAPFS
        if [[ $? -eq 0 ]]; then
                log_must $ZFS destroy $SNAPFS
        fi

        if [[ -e $SNAPDIR ]]; then
                log_must $RM -rf $SNAPDIR > /dev/null 2>&1
        fi

        if [[ -e $TESTDIR ]]; then
                log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
        fi

	if [[ -e $TMPDIR/zfs_snapshot2.${TESTCASE_ID} ]]; then
		log_must $RM -rf $TMPDIR/zfs_snapshot2.${TESTCASE_ID} > /dev/null 2>&1
	fi

}

log_assert "Verify an archive of a file system is identical to " \
    "an archive of its snapshot."

log_onexit cleanup

typeset -i COUNT=21
typeset OP=create

[[ -n $TESTDIR ]] && \
    $RM -rf $TESTDIR/* > /dev/null 2>&1

log_note "Create files in the zfs filesystem..."
populate_dir $TESTDIR/file $COUNT $NUM_WRITES $BLOCKSZ $DATA

log_note "Create a tarball from $TESTDIR contents..."
CWD=$PWD
cd $TESTDIR || log_fail "Could not cd $TESTDIR"
log_must $TAR cf $TESTDIR/tarball.original.tar file*
cd $CWD || log_fail "Could not cd $CWD"

log_note "Create a snapshot and mount it..."
log_must $ZFS snapshot $SNAPFS

log_note "Remove all of the original files..."
log_must $RM -f $TESTDIR/file* > /dev/null 2>&1

log_note "Create tarball of snapshot..."
CWD=$PWD
cd $SNAPDIR || log_fail "Could not cd $SNAPDIR"
log_must $TAR cf $TESTDIR/tarball.snapshot.tar file*
cd $CWD || log_fail "Could not cd $CWD"

log_must $MKDIR $TESTDIR/original
log_must $MKDIR $TESTDIR/snapshot

CWD=$PWD
cd $TESTDIR/original || log_fail "Could not cd $TESTDIR/original"
log_must $TAR xf $TESTDIR/tarball.original.tar

cd $TESTDIR/snapshot || log_fail "Could not cd $TESTDIR/snapshot"
log_must $TAR xf $TESTDIR/tarball.snapshot.tar

cd $CWD || log_fail "Could not cd $CWD"

$DIRCMP $TESTDIR/original $TESTDIR/snapshot > $TMPDIR/zfs_snapshot2.${TESTCASE_ID}
$GREP different $TMPDIR/zfs_snapshot2.${TESTCASE_ID} >/dev/null 2>&1
if [[ $? -ne 1 ]]; then
	log_fail "Directory structures differ."
fi

log_pass "Directory structures match."
