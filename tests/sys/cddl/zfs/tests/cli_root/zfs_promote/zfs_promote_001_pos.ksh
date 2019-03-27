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
# ident	"@(#)zfs_promote_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_promote_001_pos
#
# DESCRIPTION: 
#	'zfs promote' can promote a clone filesystem to no longer be dependent
#	on its "origin" snapshot.
#
# STRATEGY:
#	1. Create a snapshot and a clone of the snapshot
#	2. Promote the clone filesystem
#	3. Verify the promoted filesystem become independent
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
	if snapexists $csnap; then
		log_must $ZFS promote $fs
	fi
	snapexists $snap && \
		log_must $ZFS destroy -rR $snap

	typeset data
	for data in $file0 $file1; do
		[[ -e $data ]] && $RM -f $data
	done
}

function testing_verify
{
	typeset ds=$1
	typeset ds_file=$2
	typeset snap_file=$3
	typeset c_ds=$4
	typeset c_file=$5
	typeset csnap_file=$6
	typeset origin_prop=""
	
	
	snapexists $ds@$TESTSNAP && \
		log_fail "zfs promote cannot promote $ds@$TESTSNAP."
	! snapexists $c_ds@$TESTSNAP && \
		log_fail "The $c_ds@$TESTSNAP after zfs promote doesn't exist."

	origin_prop=$(get_prop origin $ds)
	[[ "$origin_prop" != "$c_ds@$TESTSNAP" ]] && \
		log_fail "The dependency of $ds is not correct."
	origin_prop=$(get_prop origin $c_ds)
	[[ "$origin_prop" != "-" ]] && \
		log_fail "The dependency of $c_ds is not correct."

	if [[ -e $snap_file ]] || [[ ! -e $csnap_file ]]; then
		log_fail "Data file $snap_file cannot be correctly promoted."
	fi
	if [[ ! -e $ds_file ]] || [[ ! -e $c_file ]]; then
        	log_fail "There exists data file losing after zfs promote."
	fi

	log_mustnot $ZFS destroy -r $c_ds
}

log_assert "'zfs promote' can promote a clone filesystem." 
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
file0=$TESTDIR/$TESTFILE0
file1=$TESTDIR/$TESTFILE1
snap=$fs@$TESTSNAP
snapfile=$TESTDIR/$(get_snapdir_name)/$TESTSNAP/$TESTFILE0
clone=$TESTPOOL/$TESTCLONE
cfile=/$clone/$CLONEFILE
csnap=$clone@$TESTSNAP
csnapfile=/$clone/$(get_snapdir_name)/$TESTSNAP/$TESTFILE0

# setup for promte testing
log_must $MKFILE $FILESIZE $file0 
log_must $ZFS snapshot $snap
log_must $MKFILE $FILESIZE $file1
log_must $RM -f $file0
log_must $ZFS clone $snap $clone
log_must $MKFILE $FILESIZE $cfile

log_must $ZFS promote $clone
# verify the 'promote' operation
testing_verify $fs $file1 $snapfile $clone $cfile $csnapfile

log_note "Verify 'zfs promote' can change back the dependency relationship."
log_must $ZFS promote $fs
#verify the result
testing_verify $clone $cfile $csnapfile $fs $file1 $snapfile 

log_pass "'zfs promote' reverses the clone parent-child dependency relationship"\
	"as expected."

