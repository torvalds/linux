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
# ident	"@(#)zfs_acl_chmod_rwacl_001_pos.ksh	1.5	09/05/19 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_rwacl_001_pos
#
# DESCRIPTION:
#	Verify assigned read_acl/write_acl to owner@/group@/everyone@,
#	specificied user and group. File have the correct access permission.
#
# STRATEGY:
#	1. Separatedly verify file and directory was assigned read_acl/write_acl
#	   by root and non-root user.
#	2. Verify owner always can read and write acl, even deny.
#	3. Verify group access permission, when group was assigned 
#	   read_acl/write_acl.
#	4. Verify access permission, after everyone was assigned read_acl/write.
#	5. Verify everyone@ was deny except specificied user, this user can read
#	   and write acl.
#	6. Verify the group was deny except specified user, this user can read
#	   and write acl
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-10-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify chmod A[number]{+|-|=} read_acl/write_acl have correct " \
	"behaviour to access permission."
log_onexit cleanup

function read_ACL #<node> <user1> <user2> ... 
{
	typeset node=$1
	typeset user
	typeset -i ret

	shift
	for user in $@; do
		chgusr_exec $user $LS -vd $node > /dev/null 2>&1
		ret=$?
		(( ret != 0 )) && return $ret

		shift
	done

	return 0
}

function write_ACL #<node> <user1> <user2> ... 
{
	typeset node=$1
	typeset user
	typeset -i ret before_cnt after_cnt

	shift
	for user in "$@"; do
		before_cnt=$(count_ACE $node)
		ret=$?; 
		(( ret != 0 )) && return $ret

		chgusr_exec $user $CHMOD A0+owner@:read_data:allow $node
		ret=$?
		(( ret != 0 )) && return $ret

		after_cnt=$(count_ACE $node)
		ret=$?
		(( ret != 0 )) && return $ret

		chgusr_exec $user $CHMOD A0- $node
		ret=$?
		(( ret != 0 )) && return $ret
		
		if (( after_cnt - before_cnt != 1 )); then
			return 1
		fi

		shift
	done

	return 0
}

function check_owner #<node>
{
	typeset node=$1

	for acc in allow deny; do
		log_must usr_exec \
			$CHMOD A0+owner@:read_acl/write_acl:$acc $node
		log_must read_ACL $node $ZFS_ACL_CUR_USER
		log_must write_ACL $node $ZFS_ACL_CUR_USER
		log_must usr_exec $CHMOD A0- $node
	done
}

function check_group #<node>
{
	typeset node=$1

	typeset grp_usr=""
	if [[ $ZFS_ACL_CUR_USER == root ]]; then
		grp_usr=$ZFS_ACL_ADMIN
	elif [[ $ZFS_ACL_CUR_USER == $ZFS_ACL_STAFF1 ]]; then
		grp_usr=$ZFS_ACL_STAFF2
	fi
		
	log_must usr_exec $CHMOD A0+group@:read_acl/write_acl:allow $node
	log_must read_ACL $node $grp_usr
	log_must write_ACL $node $grp_usr 
	log_must usr_exec $CHMOD A0- $node

	log_must usr_exec $CHMOD A0+group@:read_acl/write_acl:deny $node
	log_mustnot read_ACL $node $grp_usr
	log_mustnot write_ACL $node $grp_usr 
	log_must usr_exec $CHMOD A0- $node
}

function check_everyone #<node>
{
	typeset node=$1

	typeset flag
	for flag in allow deny; do
		if [[ $flag == allow ]]; then
			log=log_must
		else
			log=log_mustnot
		fi

		log_must usr_exec \
			$CHMOD A0+everyone@:read_acl/write_acl:$flag $node

		$log read_ACL $node $ZFS_ACL_OTHER1 $ZFS_ACL_OTHER2
		$log write_ACL $node $ZFS_ACL_OTHER1 $ZFS_ACL_OTHER2 

		log_must usr_exec $CHMOD A0- $node
	done
}

function check_spec_user #<node>
{
	typeset node=$1

	log_must usr_exec $CHMOD A0+everyone@:read_acl/write_acl:deny $node
	log_must usr_exec \
		$CHMOD A0+user:$ZFS_ACL_OTHER1:read_acl/write_acl:allow $node

	# The specified user can read and write acl
	log_must read_ACL $node $ZFS_ACL_OTHER1
	log_must write_ACL $node $ZFS_ACL_OTHER1 

	# All the other user can't read and write acl
	log_mustnot \
		read_ACL $node $ZFS_ACL_ADMIN $ZFS_ACL_STAFF2 $ZFS_ACL_OTHER2
	log_mustnot \
		write_ACL $node $ZFS_ACL_ADMIN $ZFS_ACL_STAFF2 $ZFS_ACL_OTHER2

	log_must usr_exec $CHMOD A0- $node
	log_must usr_exec $CHMOD A0- $node
}

function check_spec_group #<node>
{
	typeset node=$1

	log_must usr_exec $CHMOD A0+everyone@:read_acl/write_acl:deny $node
	log_must usr_exec $CHMOD \
		A0+group:$ZFS_ACL_OTHER_GROUP:read_acl/write_acl:allow $node
	
	# The specified group can read and write acl
	log_must read_ACL $node $ZFS_ACL_OTHER1 $ZFS_ACL_OTHER2
	log_must write_ACL $node $ZFS_ACL_OTHER1 $ZFS_ACL_OTHER2

	# All the other user can't read and write acl
	log_mustnot read_ACL $node $ZFS_ACL_ADMIN $ZFS_ACL_STAFF2
	log_mustnot write_ACL $node $ZFS_ACL_ADMIN $ZFS_ACL_STAFF2
}

function check_user_in_group #<node>
{
	typeset node=$1

	log_must usr_exec $CHMOD \
		A0+group:$ZFS_ACL_OTHER_GROUP:read_acl/write_acl:deny $node
	log_must usr_exec $CHMOD \
		A0+user:$ZFS_ACL_OTHER1:read_acl/write_acl:allow $node
	log_must read_ACL $node $ZFS_ACL_OTHER1
	log_must write_ACL $node $ZFS_ACL_OTHER1
	log_mustnot read_ACL $node $ZFS_ACL_OTHER2
	log_mustnot write_ACL $node $ZFS_ACL_OTHER2

	log_must usr_exec $CHMOD A0- $node
	log_must usr_exec $CHMOD A0- $node
}

set -A func_name check_owner \
		check_group \
		check_everyone \
		check_spec_user \
		check_spec_group \
		check_user_in_group

test_requires ZFS_ACL

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must usr_exec $TOUCH $testfile
	log_must usr_exec $MKDIR $testdir

	typeset func node
	for func in ${func_name[@]}; do
		for node in $testfile $testdir; do
			eval $func \$node
		done
	done

	log_must usr_exec $RM -rf $testfile $testdir
done

log_pass "Verify chmod A[number]{+|-|=} read_acl/write_acl passed."
