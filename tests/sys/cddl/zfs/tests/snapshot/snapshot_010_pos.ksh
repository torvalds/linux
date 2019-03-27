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
# ident	"@(#)snapshot_010_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_010_pos
#
# DESCRIPTION:
#	Verify 'destroy -r' can correctly destroy a snapshot tree at any point. 
#
# STRATEGY:
# 1. Use the snapshot -r to create snapshot for top level pool 
# 2. Select a middle point of the snapshot tree, use destroy -r to destroy all
#	snapshots beneath the point.
# 3. Verify the destroy results.
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
	typeset snap

	datasetexists $ctrvol && \
		log_must $ZFS destroy -f $ctrvol

	for snap in $ctrfs@$TESTSNAP1 \
		$snappool $snapvol $snapctr $snapctrvol \
		$snapctrclone $snapctrfs
	do
		snapexists $snap && \
			log_must $ZFS destroy -rf $snap
	done

}

log_assert "Verify 'destroy -r' can correctly destroy a snapshot subtree at any point."
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS1
ctrvol=$ctr/$TESTVOL1
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapvol=$SNAPFS1
snapctrvol=$ctr/$TESTVOL1@$TESTSNAP
snapctrclone=$ctr/$TESTCLONE@$TESTSNAP
snapctrfs=$SNAPCTR

#preparation for testing
log_must $ZFS snapshot $ctrfs@$TESTSNAP1
if is_global_zone; then
	log_must $ZFS create -V $VOLSIZE $ctrvol
else
	log_must $ZFS create $ctrvol
fi

log_must $ZFS snapshot -r $snappool

#select the $TESTCTR as destroy point, $TESTCTR is a child of $TESTPOOL
log_must $ZFS destroy -r $snapctr
for snap in $snapctr $snapctrvol $snapctrclone $snapctrfs; do
	snapexists $snap && \
		log_fail "The snapshot $snap is not destroyed correctly."
done

for snap in $snappool $snapfs $snapvol $ctrfs@$TESTSNAP1;do
	! snapexists $snap && \
		log_fail "The snapshot $snap should be not destroyed."
done

log_pass  "'destroy -r' destroys snapshot subtree as expected."
