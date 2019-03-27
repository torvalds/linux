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
# ident	"@(#)zfs_unmount_009_pos.ksh	1.2	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_unmount_009_pos
#
# DESCRIPTION:
# Verify that zfs unmount and destroy in a snapshot directory will not cause error.
#
# STRATEGY:
# 1. Create a file in a zfs filesystem, snapshot it and change directory to snapshot directory
# 2. Verify that 'zfs unmount -a'  will fail and 'zfs unmount -fa' will succeed
# 3. Verify 'ls' and 'cd /' will succeed
# 4. 'zfs mount -a' and change directory to snapshot directory again
# 5. Verify that zfs destroy snapshot will succeed
# 6. Verify 'ls' and 'cd /' will succeed
# 7. Create zfs filesystem, create a file, snapshot it and change to snapshot directory
# 8. Verify that zpool destroy the pool will succeed
# 9. Verify 'ls' 'cd /' 'zpool list' and etc will succeed
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-07-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	DISK=${DISKS%% *}

	for fs in $TESTPOOL/$TESTFS $TESTPOOL ; do
		typeset snap=$fs@$TESTSNAP
		if snapexists $snap; then
			log_must $ZFS destroy $snap
		fi
	done

	if ! poolexists $TESTPOOL && is_global_zone; then
		log_must $ZPOOL create $TESTPOOL $DISK
	fi

	if ! datasetexists $TESTPOOL/$TESTFS; then
		log_must $ZFS create $TESTPOOL/$TESTFS
		log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	fi
}

function restore_dataset
{
	if ! datasetexists $TESTPOOL/$TESTFS ; then
		log_must $ZFS create $TESTPOOL/$TESTFS
		log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
		log_must cd $TESTDIR
		$ECHO hello > world 
		log_must $ZFS snapshot $TESTPOOL/$TESTFS@$TESTSNAP
		log_must cd $(get_snapdir_name)/$TESTSNAP
	fi
}


log_assert "zfs fource unmount and destroy in snapshot directory will not cause error."
log_onexit cleanup

for fs in $TESTPOOL/$TESTFS $TESTPOOL ; do
	typeset snap=$fs@$TESTSNAP
	typeset mtpt=$(get_prop mountpoint $fs)

	log_must cd $mtpt
	$ECHO hello > world 
	log_must $ZFS snapshot $snap
	log_must cd $(get_snapdir_name)/$TESTSNAP

	log_mustnot $ZFS unmount -a
	log_must $ZFS unmount -fa
	log_mustnot $LS
	log_must cd /

	log_must $ZFS mount -a
	log_must cd $mtpt
	log_must cd $(get_snapdir_name)/$TESTSNAP

	if is_global_zone || [[ $fs != $TESTPOOL ]] ; then
		log_must $ZFS destroy -rf $fs
		log_mustnot $LS
		log_must cd /
	fi

	restore_dataset
done

if is_global_zone ; then
	log_must $ZPOOL destroy -f $TESTPOOL
	log_mustnot $LS
	log_must cd /
fi

log_must eval $ZFS list > /dev/null 2>&1
log_must eval $ZPOOL list > /dev/null 2>&1
log_must eval $ZPOOL status > /dev/null 2>&1
$ZPOOL iostat > /dev/null 2>&1

log_pass "zfs fource unmount and destroy in snapshot directory will not cause error."
