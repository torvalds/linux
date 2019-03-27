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
# ident	"@(#)zfs_rename_007_pos.ksh	1.2	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_007_pos
#
# DESCRIPTION:
#	Rename dataset, verify that the data haven't changed.
#
# STRATEGY:
#	1. Create random data and copy to dataset.
#	2. Perform renaming commands.
#	3. Verify that the data haven't changed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-03-15)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

# Check if current system support recursive rename
$ZFS rename 2>&1 | grep "rename -r" >/dev/null 2>&1
if (($? != 0)); then
	log_unsupported
fi

function cleanup
{
	if datasetexists $TESTPOOL/$TESTFS ; then
		log_must $ZFS destroy -Rf $TESTPOOL/$TESTFS
	fi
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	$RM -f $SRC_FILE $DST_FILE
}

function target_obj
{
	typeset dtst=$1

	typeset obj
	typeset type=$(get_prop type $dtst)
	if [[ $type == "filesystem" ]]; then
		obj=$(get_prop mountpoint $dtst)/${SRC_FILE##*/}
	elif [[ $type == "volume" ]]; then
		obj=/dev/zvol/$dtst
	fi

	print $obj
}

log_assert "Rename dataset, verify that the data haven't changed."
log_onexit cleanup

# Generate random data
#
BS=512 ; CNT=2048
SRC_FILE=$TMPDIR/srcfile.${TESTCASE_ID}
DST_FILE=$TMPDIR/dstfile.${TESTCASE_ID}
log_must $DD if=/dev/random of=$SRC_FILE bs=$BS count=$CNT

fs=$TESTPOOL/$TESTFS/fs.${TESTCASE_ID}
fsclone=$TESTPOOL/$TESTFS/fsclone.${TESTCASE_ID} 
log_must $ZFS create $fs

obj=$(target_obj $fs)
log_must $CP $SRC_FILE $obj

snap=${fs}@snap.${TESTCASE_ID}
log_must $ZFS snapshot $snap
log_must $ZFS clone $snap $fsclone

# Rename dataset & clone
#
log_must $ZFS rename $fs ${fs}-new
log_must $ZFS rename $fsclone ${fsclone}-new

# Compare source file and target file
#
obj=$(target_obj ${fs}-new)
log_must $DIFF $SRC_FILE $obj
obj=$(target_obj ${fsclone}-new)
log_must $DIFF $SRC_FILE $obj

# Rename snapshot and re-clone dataset
#
log_must $ZFS rename ${fs}-new $fs
log_must $ZFS rename $snap ${snap}-new
log_must $ZFS clone ${snap}-new $fsclone

# Compare source file and target file
#
obj=$(target_obj $fsclone)
log_must $DIFF $SRC_FILE $obj

if is_global_zone; then
	vol=$TESTPOOL/$TESTFS/vol.${TESTCASE_ID} ;	volclone=$TESTPOOL/$TESTFS/volclone.${TESTCASE_ID}
	log_must $ZFS create -V 100M $vol

	obj=$(target_obj $vol)
	log_must $DD if=$SRC_FILE of=$obj bs=$BS count=$CNT

	snap=${vol}@snap.${TESTCASE_ID}
	log_must $ZFS snapshot $snap
	log_must $ZFS clone $snap $volclone

	# Rename dataset & clone
	log_must $ZFS rename $vol ${vol}-new
	log_must $ZFS rename $volclone ${volclone}-new

	# Compare source file and target file
	obj=$(target_obj ${vol}-new)
	log_must $DD if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must $DIFF $SRC_FILE $DST_FILE
	obj=$(target_obj ${volclone}-new)
	log_must $DD if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must $DIFF $SRC_FILE $DST_FILE

	# Rename snapshot and re-clone dataset
	log_must $ZFS rename ${vol}-new $vol
	log_must $ZFS rename $snap ${snap}-new
	log_must $ZFS clone ${snap}-new $volclone

	# Compare source file and target file
	obj=$(target_obj $volclone)
	log_must $DD if=$obj of=$DST_FILE bs=$BS count=$CNT
	log_must $DIFF $SRC_FILE $DST_FILE
fi

log_pass "Rename dataset, the data haven't changed passed."
