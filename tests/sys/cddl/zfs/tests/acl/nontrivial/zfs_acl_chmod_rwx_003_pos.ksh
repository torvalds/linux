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
# ident	"@(#)zfs_acl_chmod_rwx_003_pos.ksh	1.3	07/07/31 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_rwx_003_pos
#
# DESCRIPTION:
#	Verify that the read_data/write_data/execute permission for 
#	owner/group/everyone are correct.
#
# STRATEGY:
#	1. Loop root and non-root user.
#	2. Separated verify type@:access:allow|deny to file and directory
#	3. To super user, read and write deny was override.
#	4. According to ACE list and override rule, expect that 
#	   read/write/execute file or directory succeed or fail.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-10-09)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

#	owner@		group_users		other_users
set -A users \
	"root" 		"$ZFS_ACL_ADMIN" 	"$ZFS_ACL_OTHER1" \
	"$ZFS_ACL_STAFF1" "$ZFS_ACL_STAFF2" 	"$ZFS_ACL_OTHER1"

# In order to test execute permission, read_data was need firstly.
set -A a_access "read_data" "write_data" "read_data/execute"
set -A a_flag "owner@" "group@" "everyone@"

log_assert "Verify that the read_data/write_data/execute permission for" \
	"owner/group/everyone are correct."
log_onexit cleanup

function logname #node acl_spec user
{
	typeset node=$1
	typeset acl_spec=$2
	typeset user=$3

	# To super user, read and write deny permission was override.
	if [[ $acl_spec == *:allow ]] || \
		[[ $user == root && -d $node ]] || \
		[[ $user == root && $acl_spec != *"execute"* ]]
	then
		print "log_must"
	elif [[ $acl_spec == *:deny ]]; then
		print "log_mustnot"
	fi
}

function check_chmod_results #node acl_spec g_usr o_usr
{
	typeset node=$1
	typeset acl_spec=$2
	typeset g_usr=$3
	typeset o_usr=$4
	typeset log

	if [[ $acl_spec == "owner@:"* || $acl_spec == "everyone@:"* ]]; then
		log=$(logname $node $acl_spec $ZFS_ACL_CUR_USER)
		$log rwx_node $ZFS_ACL_CUR_USER $node $acl_spec
	fi
	if [[ $acl_spec == "group@:"* || $acl_spec == "everyone@:"* ]]; then
		log=$(logname $node $acl_spec $g_usr)
		$log rwx_node $g_usr $node $acl_spec
	fi
	if [[ $acl_spec == "everyone@"* ]]; then
		log=$(logname $node $acl_spec $o_usr)
		$log rwx_node $o_usr $node $acl_spec
	fi
}

function test_chmod_basic_access #node group_user other_user
{
	typeset node=$1
	typeset g_usr=$2
	typeset o_usr=$3
	typeset flag access acl_spec

	for flag in ${a_flag[@]}; do
		for access in ${a_access[@]}; do
			for tp in allow deny; do
				acl_spec="$flag:$access:$tp"
				log_must usr_exec $CHMOD A+$acl_spec $node
				check_chmod_results \
					$node $acl_spec $g_usr $o_usr
				log_must usr_exec $CHMOD A0- $node
			done
		done	
	done
}

test_requires ZFS_ACL

typeset -i i=0
while (( i < ${#users[@]} )); do
	log_must set_cur_usr ${users[i]}

	log_must usr_exec $TOUCH $testfile
	test_chmod_basic_access $testfile ${users[((i+1))]} ${users[((i+2))]}
	log_must usr_exec $MKDIR $testdir
	test_chmod_basic_access $testdir ${users[((i+1))]} ${users[((i+2))]}

	log_must usr_exec $RM -rf $testfile $testdir

	(( i += 3 ))
done

log_pass "Verify that the read_data/write_data/execute permission for" \
	"owner/group/everyone passed."
