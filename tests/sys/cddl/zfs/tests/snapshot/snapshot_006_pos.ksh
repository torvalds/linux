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
# ident	"@(#)snapshot_006_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_006_pos
#
# DESCRIPTION:
# An archive of a zfs dataset and an archive of its snapshot
# changed sinced the snapshot was taken.
#
# STRATEGY:
# 1) Create some files in a ZFS dataset
# 2) Create a tarball of the dataset
# 3) Create a snapshot of the dataset
# 4) Remove all the files in the original dataset
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

        snapexists $SNAPCTR
        if [[ $? -eq 0 ]]; then
                log_must $ZFS destroy $SNAPCTR
        fi

        if [[ -e $SNAPDIR1 ]]; then
                log_must $RM -rf $SNAPDIR1 > /dev/null 2>&1
        fi

        if [[ -e $TESTDIR1 ]]; then
                log_must $RM -rf $TESTDIR1/* > /dev/null 2>&1
        fi

	if [[ -e $TMPDIR/zfs_snapshot2.${TESTCASE_ID} ]]; then
		log_must $RM -rf $TMPDIR/zfs_snapshot2.${TESTCASE_ID} > /dev/null 2>&1
	fi

}

log_assert "Verify that an archive of a dataset is identical to " \
   "an archive of the dataset's snapshot."

log_onexit cleanup

typeset -i COUNT=21
typeset OP=create

[[ -n $TESTDIR1 ]] && $RM -rf $TESTDIR1/* > /dev/null 2>&1

log_note "Create files in the zfs dataset ..."
populate_dir $TESTDIR1/file $COUNT $NUM_WRITES $BLOCKSZ $DATA

log_note "Create a tarball from $TESTDIR1 contents..."
CWD=$PWD
cd $TESTDIR1 || log_fail "Could not cd $TESTDIR1"
log_must $TAR cf $TESTDIR1/tarball.original.tar file*
cd $CWD || log_fail "Could not cd $CWD"

log_note "Create a snapshot and mount it..."
log_must $ZFS snapshot $SNAPCTR

log_note "Remove all of the original files..."
log_must $RM -f $TESTDIR1/file* > /dev/null 2>&1

log_note "Create tarball of snapshot..."
CWD=$PWD
cd $SNAPDIR1 || log_fail "Could not cd $SNAPDIR1"
log_must $TAR cf $TESTDIR1/tarball.snapshot.tar file*
cd $CWD || log_fail "Could not cd $CWD"

log_must $MKDIR $TESTDIR1/original
log_must $MKDIR $TESTDIR1/snapshot

CWD=$PWD
cd $TESTDIR1/original || log_fail "Could not cd $TESTDIR1/original"
log_must $TAR xf $TESTDIR1/tarball.original.tar

cd $TESTDIR1/snapshot || log_fail "Could not cd $TESTDIR1/snapshot"
log_must $TAR xf $TESTDIR1/tarball.snapshot.tar

cd $CWD || log_fail "Could not cd $CWD"

$DIRCMP $TESTDIR1/original $TESTDIR1/snapshot > $TMPDIR/zfs_snapshot2.${TESTCASE_ID}
$GREP different $TMPDIR/zfs_snapshot2.${TESTCASE_ID} >/dev/null 2>&1
if [[ $? -ne 1 ]]; then
	log_fail "Directory structures differ."
fi

log_pass "Directory structures match."
