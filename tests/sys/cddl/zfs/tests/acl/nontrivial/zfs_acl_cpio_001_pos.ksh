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
# ident	"@(#)zfs_acl_cpio_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/tests/acl/acl_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_cpio_001_pos
#
# DESCRIPTION:
# Verify that '$CPIO' command with -P option supports to archive ZFS ACLs
#
# STRATEGY:
# 1. Create file and directory in zfs filesystem
# 2. Add new ACE in ACL or change mode of file and directory
# 3. Use $CPIO to archive file and directory
# 4. Extract the archive file
# 5. Verify that the restored ACLs of file and directory identify
#    with the origional ones. 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if datasetexists $TESTPOOL/$TESTFS1; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS1
	fi
	if (( ${#orig_dir} != 0 )); then
		cd $orig_dir
	fi
	[[ -d $TESTDIR1 ]] && log_must $RM -rf $TESTDIR1
	[[ -d $TESTDIR ]] && log_must $RM -rf $TESTDIR/*
}

log_assert "Verify that '$CPIO' command supports to archive ZFS ACLs."
log_onexit cleanup

test_requires ZFS_ACL

set -A ops "A+everyone@:execute:allow" \
	"A3+user:$ZFS_ACL_OTHER1:write_data:deny" \
	"A5+group:$ZFS_ACL_OTHER_GROUP:read_data:deny" \
	"A0+user:$ZFS_ACL_OTHER1:write_data:deny" \
	"A1=user:$ZFS_ACL_STAFF1:write_data:deny" \
	"A5=group:$ZFS_ACL_STAFF_GROUP:write_data:deny"

log_note "Create second zfs file system to restore the cpio archive."
log_must $ZFS create $TESTPOOL/$TESTFS1
log_must $ZFS set mountpoint=$TESTDIR1 $TESTPOOL/$TESTFS1
log_must $CHMOD 777 $TESTDIR1

# Define test fine and record the original directory.
CPIOFILE=cpiofile.${TESTCASE_ID}
file=$TESTFILE0
dir=dir.${TESTCASE_ID}
orig_dir=$PWD

typeset user
for user in root $ZFS_ACL_STAFF1; do
	# Set the current user
	log_must set_cur_usr $user

	typeset -i i=0
	while (( i < ${#ops[*]} )); do
		log_note "Create file $file and directory $dir " \
			"in zfs filesystem. "
		cd $TESTDIR
		log_must usr_exec $TOUCH $file
		log_must usr_exec $MKDIR $dir

		log_note "Change the ACLs of file and directory with " \
			"'$CHMOD ${ops[i]}'." 
		for obj in $file $dir; do
			log_must usr_exec $CHMOD ${ops[i]} $obj
		done

		log_note "Archive the file and directory."
		cd $TESTDIR
		log_must eval "usr_exec $LS | " \
			"usr_exec $CPIO -ocP -O $CPIOFILE > /dev/null 2>&1"

		log_note "Restore the cpio archive."
		log_must usr_exec $MV $CPIOFILE $TESTDIR1
		cd $TESTDIR1
		log_must eval "usr_exec $CAT $CPIOFILE | " \
			"usr_exec $CPIO -icP > /dev/null 2>&1"

		log_note "Verify that the ACLs of restored file/directory " \
			"have no changes."
		for obj in $file $dir; do
			log_must compare_modes $TESTDIR/$obj $TESTDIR1/$obj
			log_must compare_acls $TESTDIR/$obj $TESTDIR1/$obj
		done

		log_must usr_exec $RM -rf $TESTDIR/* $TESTDIR1/*

		(( i = i + 1 ))
	done
done

log_pass "'$CPIO' command succeeds to support ZFS ACLs."
