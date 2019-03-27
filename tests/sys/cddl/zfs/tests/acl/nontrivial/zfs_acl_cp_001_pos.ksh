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
# ident	"@(#)zfs_acl_cp_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_cp_001_pos
#
# DESCRIPTION:
# 	Verify that '/bin/cp [-p]' supports ZFS ACL
#
# STRATEGY:
# 	1. Create file and  directory in zfs filesystem
# 	2. Set special ACE to the file and directory
# 	3. Copy the file/directory within and across zfs file system
# 	4. Verify that the ACL of file/directroy is not changed, when you are
# 	   inserting an ACL with a user: or group: entry on the top.
#	   (abstractions entry are treated special, since they represent the 
#	   traditional permission bit mapping.)
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-10-11)
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
	[[ -d $TESTDIR1 ]] && log_must $RM -rf $TESTDIR1
	[[ -d $TESTDIR ]] && log_must $RM -rf $TESTDIR/*
}

log_assert "Verify that '$CP [-p]' supports ZFS ACLs."
log_onexit cleanup

test_requires ZFS_ACL

log_note "Create the second zfs file system: $TESTPOOL/$TESTFS1."
log_must $ZFS create $TESTPOOL/$TESTFS1
log_must $ZFS set mountpoint=$TESTDIR1 $TESTPOOL/$TESTFS1
log_must $ZFS set aclmode=passthrough $TESTPOOL/$TESTFS1
log_must $CHMOD 777 $TESTDIR1

# Define target directory.
dstdir=$TESTDIR1/dstdir.${TESTCASE_ID}

for user in root $ZFS_ACL_STAFF1; do
	# Set the current user
	log_must set_cur_usr $user

	for obj in $testfile $testdir; do
		# Create source object and target directroy
		log_must usr_exec $TOUCH $testfile
		log_must usr_exec $MKDIR $testdir $dstdir

		# Add the new ACE on the head.
		log_must usr_exec $CHMOD \
			A0+user:$ZFS_ACL_OTHER1:read_acl:deny $obj

		cmd_str="$CP -p"
		[[ -d $obj ]] && cmd_str="$CP -rp"
		log_must usr_exec $cmd_str $obj $dstdir
		log_must usr_exec $cmd_str $obj $TESTDIR1

		for dir in $dstdir $TESTDIR1; do 
			log_must compare_modes $obj $dir/${obj##*/}
			log_must compare_acls $obj $dir/${obj##*/}
		done

		# Delete all the test file and directory
		log_must usr_exec $RM -rf $TESTDIR/* $TESTDIR1/*
	done
done

log_pass "'$CP [-p]' succeeds to support ZFS ACLs."
