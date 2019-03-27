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
# ident	"@(#)history_001_pos.ksh	1.3	07/05/25 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_001_pos
#
# DESCRIPTION:
#	Create a scenario to verify the following zpool subcommands are logged.
#	    create, destroy, add, remove, offline, online, attach, detach, replace,
#	    scrub, export, import, clear, upgrade.
#
# STRATEGY:
#	1. Create three virtual disk files.
#	2. Create a three-way mirror.
#	3. Invoke every sub-commands to this mirror, except upgrade.
#	4. Compare 'zpool history' log with expected log.
#	5. Imported specified pool and upgrade it, verify 'upgrade' was logged.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $MPOOL
	destroy_pool $upgrade_pool

	[[ -d $import_dir ]] && $RM -rf $import_dir
	for file in $REAL_HISTORY $EXPECT_HISTORY \
	    $VDEV1 $VDEV2 $VDEV3 $VDEV4
	do
		[[ -f $file ]] && $RM -f $file
	done
}

log_assert "Verify zpool sub-commands which modify state are logged."
log_onexit cleanup

(( $? != 0)) && log_fail "get_prop($TESTPOOL mountpoint)"
VDEV1=$TMPDIR/vdev1; VDEV2=$TMPDIR/vdev2;
VDEV3=$TMPDIR/vdev3; VDEV4=$TMPDIR/vdev4;

log_must create_vdevs $VDEV1 $VDEV2 $VDEV3 $VDEV4
$CAT /dev/null > $EXPECT_HISTORY

exec_record $ZPOOL create $MPOOL mirror $VDEV1 $VDEV2
exec_record $ZPOOL add -f $MPOOL spare $VDEV3
exec_record $ZPOOL remove $MPOOL $VDEV3
exec_record $ZPOOL offline $MPOOL $VDEV1
exec_record $ZPOOL online $MPOOL $VDEV1
exec_record $ZPOOL attach $MPOOL $VDEV1 $VDEV4
exec_record $ZPOOL detach $MPOOL $VDEV4
exec_record $ZPOOL replace -f $MPOOL $VDEV1 $VDEV4
exec_record $ZPOOL export $MPOOL
exec_record $ZPOOL import -d $TMPDIR $MPOOL
exec_record $ZPOOL destroy $MPOOL
exec_record $ZPOOL import -D -f -d $TMPDIR $MPOOL
exec_record $ZPOOL clear $MPOOL 

format_history $MPOOL $REAL_HISTORY
log_must $DIFF $REAL_HISTORY $EXPECT_HISTORY

import_dir=$TMPDIR/import_dir.${TESTCASE_ID}
log_must $MKDIR $import_dir
log_must $CP $STF_SUITE/tests/history/zfs-pool-v4.dat.Z $import_dir
log_must $UNCOMPRESS $import_dir/zfs-pool-v4.dat.Z

# Truncate $EXPECT_HISTORY file
log_must eval "$CAT /dev/null > $EXPECT_HISTORY"

upgrade_pool=$($ZPOOL import -d $import_dir | $GREP "pool:" | $AWK '{print $2}')
exec_record $ZPOOL import -d $import_dir $upgrade_pool
# Get existing history
format_history $upgrade_pool $EXPECT_HISTORY
exec_record $ZPOOL upgrade $upgrade_pool

format_history $upgrade_pool $REAL_HISTORY
log_must $DIFF $REAL_HISTORY $EXPECT_HISTORY

log_pass "zpool sub-commands which modify state are logged passed. "
