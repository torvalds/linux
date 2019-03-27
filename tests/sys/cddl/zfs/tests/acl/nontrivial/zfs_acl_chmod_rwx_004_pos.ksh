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
# ident	"@(#)zfs_acl_chmod_rwx_004_pos.ksh	1.3	07/07/31 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_rwx_004_pos
#
# DESCRIPTION:
#	Verify that explicit ACL setting to specified user or group will
#	override existed access rule.
#
# STRATEGY:
#	1. Loop root and non-root user.
#	2. Loop the specified access one by one.
#	3. Loop verify explicit ACL set to specified user and group.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-10-14)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function check_access #log user node access rflag
{
	typeset log=$1
	typeset user=$2
	typeset node=$3
	typeset access=$4
	typeset rflag=$5

	if [[ $rflag == "allow" && $access == execute ]]; then
		rwx_node $user $node $access
		#
		# When everyone@ were deny, this file can't execute.
		# So,'cannot execute' means user has the permission to
		# execute, just the file can't be execute.
		#
		if [[ $ZFS_ACL_ERR_STR == *"cannot execute" ]]; then
			log_note "SUCCESS: rwx_node $user $node $access"
		else
			log_fail "FAIL: rwx_node $user $node $access"
		fi
	else
		$log rwx_node $user $node $access
	fi
}

function verify_explicit_ACL_rule #node access flag
{
	set -A a_access "read_data" "write_data" "execute"
	typeset node=$1
	typeset access=$2
	typeset flag=$3 
	typeset log rlog rflag

	# Get the expect log check
	if [[ $flag == allow ]]; then
		log=log_mustnot
		rlog=log_must
		rflag=deny
	else
		log=log_must
		rlog=log_mustnot
		rflag=allow
	fi

	log_must usr_exec $CHMOD A+everyone@:$access:$flag $node
	log_must usr_exec $CHMOD A+user:$ZFS_ACL_OTHER1:$access:$rflag $node
	check_access $log $ZFS_ACL_OTHER1 $node $access $rflag
	log_must usr_exec $CHMOD A0- $node

	log_must usr_exec \
		$CHMOD A+group:$ZFS_ACL_OTHER_GROUP:$access:$rflag $node
	check_access $log $ZFS_ACL_OTHER1 $node $access $rflag
	check_access $log $ZFS_ACL_OTHER2 $node $access $rflag
	log_must usr_exec $CHMOD A0- $node
	log_must usr_exec $CHMOD A0- $node

	log_must usr_exec \
		$CHMOD A+group:$ZFS_ACL_OTHER_GROUP:$access:$flag $node
	log_must usr_exec $CHMOD A+user:$ZFS_ACL_OTHER1:$access:$rflag $node
	$log rwx_node $ZFS_ACL_OTHER1 $node $access
	$rlog rwx_node $ZFS_ACL_OTHER2 $node $access
	log_must usr_exec $CHMOD A0- $node
	log_must usr_exec $CHMOD A0- $node
}

log_assert "Verify that explicit ACL setting to specified user or group will" \
	"override existed access rule."
log_onexit cleanup

set -A a_access "read_data" "write_data" "execute"
set -A a_flag "allow" "deny"
typeset node

test_requires ZFS_ACL

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must usr_exec $TOUCH $testfile
	log_must usr_exec $MKDIR $testdir
	log_must usr_exec $CHMOD 755 $testfile $testdir

	for node in $testfile $testdir; do
		for access in ${a_access[@]}; do
			for flag in ${a_flag[@]}; do
				verify_explicit_ACL_rule $node $access $flag
			done
		done
	done

	log_must usr_exec $RM -rf $testfile $testdir
done

log_pass "Explicit ACL setting to specified user or group will override " \
	"existed access rule passed."
