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
# ident	"@(#)snapshot_015_pos.ksh	1.5	09/01/12 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_015_pos
#
# DESCRIPTION:
#	Verify snapshot can be created or destroy via mkdir or rm 
#	in $(get_snapdir_name).
#
# STRATEGY:
#	1. Verify make directories only successfully in $(get_snapdir_name).
#	2. Verify snapshot can be created and destroy via mkdir and remove
#	directories in $(get_snapdir_name).
#	3. Verify rollback to previous snapshot can succeed.
#	4. Verify remove directory in snapdir can destroy snapshot.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-10-17)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset -i i=0
	while ((i < snap_cnt)); do
		typeset snap=$fs@snap.$i
		datasetexists $snap && log_must $ZFS destroy -f $snap

		((i += 1))
	done
}

$ZFS 2>&1 | $GREP "allow" > /dev/null
(($? != 0)) && log_unsupported

log_assert "Verify snapshot can be created via mkdir in $(get_snapdir_name)."
log_onexit cleanup

[[ $os_name == "FreeBSD" ]] && \
	log_uninitiated "Directory operations on the $(get_snapdir_name) directory are not yet supported in FreeBSD"

fs=$TESTPOOL/$TESTFS
# Verify all the other directories are readonly.
mntpnt=$(get_prop mountpoint $fs)
snapdir=$mntpnt/.zfs
set -A ro_dirs "$snapdir" "$snapdir/snap" "$snapdir/snapshot"
for dir in ${ro_dirs[@]}; do
	if [[ -d $dir ]]; then
		log_mustnot $RM -rf $dir
		log_mustnot $TOUCH $dir/testfile
	else
		log_mustnot $MKDIR $dir
	fi
done

# Verify snapshot can be created via mkdir in $(get_snapdir_name)
typeset -i snap_cnt=5
typeset -i cnt=0
while ((cnt < snap_cnt)); do
	testfile=$mntpnt/testfile.$cnt
	log_must $MKFILE 1M $testfile
	log_must $MKDIR $mntpnt/$(get_snapdir_name)/snap.$cnt
	if ! datasetexists $fs@snap.$cnt ; then
		log_fail "ERROR: $fs@snap.$cnt should exists."
	fi

	((cnt += 1))
done

# Verify rollback to previous snapshot succeed.
((cnt = RANDOM % snap_cnt))
log_must $ZFS rollback -r $fs@snap.$cnt

typeset -i i=0
while ((i < snap_cnt)); do
	testfile=$mntpnt/testfile.$i
	if ((i <= cnt)); then
		if [[ ! -f $testfile ]]; then
			log_fail "ERROR: $testfile should exists."
		fi
	else
		if [[ -f $testfile ]]; then
			log_fail "ERROR: $testfile should not exists."
		fi
	fi
		
	((i += 1))
done

# Verify remove directory in snapdir can destroy snapshot.
log_must $RMDIR $mntpnt/$(get_snapdir_name)/snap.$cnt
log_mustnot datasetexists $fs@snap.$cnt

log_pass "Verify snapshot can be created via mkdir in $(get_snapdir_name) passed."
