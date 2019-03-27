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
# ident	"@(#)snapshot_013_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_013_pos
#
# DESCRIPTION:
#	verify that the snapshots created by 'snapshot -r' can be used for 
#	zfs send/recv 
#
# STRATEGY:
#	1. create a dataset tree and populate a filesystem
#	2. snapshot -r the dataset tree
#	3. select one snapshot used  for zfs send/recv
#	4. verify the data integrity after zfs send/recv
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	datasetexists $ctrfs && \
		$ZFS destroy -r $ctrfs

	snapexists $snappool && \
		log_must $ZFS destroy -r $snappool

	[[ -e $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
}

log_assert "Verify snapshots from 'snapshot -r' can be used for zfs send/recv"
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapctrfs=$ctrfs@$TESTSNAP
fsdir=/$ctrfs
snapdir=$fsdir/$(get_snapdir_name)/$TESTSNAP

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to snapshot)"
populate_dir $TESTDIR/file $COUNT $NUM_WRITES $BLOCKSZ ITER

log_must $ZFS snapshot -r $snappool

$ZFS send $snapfs | $ZFS receive $ctrfs >/dev/null 2>&1
if ! datasetexists $ctrfs || ! snapexists $snapctrfs; then
	log_fail "zfs send/receive fails with snapshot $snapfs."
fi

for dir in $fsdir $snapdir; do
	FILE_COUNT=`$LS -Al $dir | $GREP -v "total" | wc -l`
	(( FILE_COUNT != COUNT )) && \
        	log_fail "The data gets changed after zfs send/recv."
done

log_pass "'zfs send/receive' works as expected with snapshots from 'snapshot -r'"
