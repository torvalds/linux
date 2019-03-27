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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)history_008_pos.ksh	1.3	09/01/12 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_008_pos
#
# DESCRIPTION:
#	Internal journal records all the recursively operations.
#
# STRATEGY:
#	1. Create a filesystem and several sub-filesystems in it.
#	2. Make recursively snapshot.
#	3. Verify internal journal records all the recursively operations.
#	4. Do the same verification to inherit, rollback and destroy.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-22)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

$ZFS 2>&1 | $GREP "allow" > /dev/null
(($? != 0)) && log_unsupported

function cleanup
{
	[[ -f $REAL_HISTORY ]] && $RM -f $REAL_HISTORY	
	[[ -f $ADD_HISTORY ]] && $RM -f $ADD_HISTORY
	if datasetexists $root_testfs; then
		log_must $ZFS destroy -rf $root_testfs
	fi
	log_must $ZFS create $root_testfs
}

log_assert "Internal journal records all the recursively operations."
log_onexit cleanup

root_testfs=$TESTPOOL/$TESTFS
fs1=$root_testfs/fs1; fs2=$root_testfs/fs2; fs3=$root_testfs/fs3
for fs in $fs1 $fs2 $fs3; do
	log_must $ZFS create $fs
done

#
# Verify 'zfs snapshot -r'
#
format_history $TESTPOOL $REAL_HISTORY -i
log_must $ZFS snapshot -r ${root_testfs}@snap
additional_history $TESTPOOL $ADD_HISTORY -i
for ds in $fs1 $fs2 $fs3 ; do
	log_must verify_history $ADD_HISTORY "snapshot" ${ds}@snap
done

log_must $ZFS snapshot ${root_testfs}@snap2
log_must $ZFS snapshot ${root_testfs}@snap3
typeset snap2_id=$(get_dataset_id ${root_testfs}@snap2)
typeset snap3_id=$(get_dataset_id ${root_testfs}@snap3)

#
# Verify 'zfs rollback -r'
#
format_history $TESTPOOL $REAL_HISTORY -i
log_must $ZFS rollback -r ${root_testfs}@snap
additional_history $TESTPOOL $ADD_HISTORY -i

cat $ADD_HISTORY
for ds_id in ${snap2_id} ${snap3_id}; do
	log_must verify_destroyed $ADD_HISTORY $ds_id
done
log_must verify_direct_history $ADD_HISTORY "rollback -r" $root_testfs

#
# Verify 'zfs inherit -r'
#
format_history $TESTPOOL $REAL_HISTORY -i
log_must $ZFS inherit -r mountpoint $root_testfs
additional_history $TESTPOOL $ADD_HISTORY -i
cat $ADD_HISTORY
for ds in $fs1 $fs2 $fs3 $root_testfs; do
	 log_must verify_history $ADD_HISTORY "inherit" $ds
done
log_must verify_direct_history $ADD_HISTORY "inherit -r mountpoint" $root_testfs

# Initial original $REAL_HISTORY 
format_history $TESTPOOL $REAL_HISTORY -i

fs1_id=$(get_dataset_id $fs1)
fs2_id=$(get_dataset_id $fs2)
fs3_id=$(get_dataset_id $fs3)
root_id=$(get_dataset_id $root_testfs)
fs1_snap_id=$(get_dataset_id ${fs1}@snap)
fs2_snap_id=$(get_dataset_id ${fs2}@snap)
fs3_snap_id=$(get_dataset_id ${fs3}@snap)
root_snap_id=$(get_dataset_id ${root_testfs}@snap)

#
# Verify 'zfs destroy -r'
#
log_must $ZFS destroy -r $root_testfs
additional_history $TESTPOOL $ADD_HISTORY -i
cat $ADD_HISTORY
for ds_id in ${fs1_id} ${fs2_id} ${fs3_id} ${root_id} ${fs1_snap_id} ${fs2_snap_id} ${fs3_snap_id} ${root_snap_id}; do
	log_must verify_destroyed $ADD_HISTORY $ds_id
done
log_must verify_direct_history $ADD_HISTORY "destroy -r" $root_testfs

log_pass "Internal journal records all the recursively operations passed."
