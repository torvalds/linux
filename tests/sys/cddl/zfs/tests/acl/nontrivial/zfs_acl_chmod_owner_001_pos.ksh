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
# ident	"@(#)zfs_acl_chmod_owner_001_pos.ksh	1.4	09/01/13 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_owner_001_pos
#
# DESCRIPTION:
#	Verify that the write_owner for 
#	owner/group/everyone are correct.
#
# STRATEGY:
# 1. Create file and  directory in zfs filesystem
# 2. Set special write_owner ACE to the file and directory
# 3. Try to chown/chgrp of the file and directory to take owner/group
# 4. Verify that the owner/group are correct. Follow these rules:
#  	(1) If uid is granted the write_owner permission, 
#		then it can only do chown to its own uid, 
#		or a group that they are a member of.
#	(2) Owner will ignore permission of (1) even write_owner not granted.
#	(3) Superuser will always permit whatever they do.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-10-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	[[ ! -e $TESTDIR/$ARCHIVEFILE ]] && return 0

	if [[ ! -e $target ]]; then
		log_must $TAR xpf $TESTDIR/$ARCHIVEFILE
	fi

	(( ${#cwd} != 0 )) && cd $cwd
	cleanup_test_files $TESTDIR/basedir
	log_must $RM -f $TESTDIR/$ARCHIVEFILE
	return 0
}

#owner@	          group                  group_users       other_users
set -A users \
"root"            "root"                 "$ZFS_ACL_ADMIN"  "$ZFS_ACL_OTHER1" \
"$ZFS_ACL_STAFF1" "$ZFS_ACL_STAFF_GROUP" "$ZFS_ACL_STAFF2" "$ZFS_ACL_OTHER1"

set -A a_access \
	"write_owner:allow" \
	"write_owner:deny"

set -A a_flag "owner@" "group@" "everyone@"

log_assert "Verify that the chown/chgrp could take owner/group " \
	"while permission is granted."
log_onexit cleanup

#
# Get the owner of a file/directory
#
function get_owner #node
{
	typeset node=$1
	typeset value

	if [[ -z $node ]]; then
		log_fail "node are not defined."
	fi

	if [[ -d $node ]]; then
		value=$($LS -dl $node | $AWK '{print $3}')
	elif [[ -e $node ]]; then
		value=$($LS -l $node | $AWK '{print $3}')
	fi

	$ECHO $value
}

#
# Get the group of a file/directory
#
function get_group #node
{
	typeset node=$1
	typeset value

	if [[ -z $node ]]; then
		log_fail "node are not defined."
	fi

	if [[ -d $node ]]; then
		value=$($LS -dl $node | $AWK '{print $4}')
	elif [[ -e $node ]]; then
		value=$($LS -l $node | $AWK '{print $4}')
	fi

	$ECHO $value
}


#
# Get the group name that a UID belongs to
#
function get_user_group #uid
{
	typeset uid=$1
	typeset value

	if [[ -z $uid ]]; then
		log_fail "UID not defined."
	fi

	value=$(id $uid)

	if [[ $? -eq 0 ]]; then
		value=${value##*\(}
		value=${value%%\)*}
		$ECHO $value
	else
		log_fail "Invalid UID (uid)."
	fi
}

function operate_node_owner #user node old_owner expect_owner
{
	typeset user=$1
	typeset node=$2
	typeset old_owner=$3
	typeset expect_owner=$4
	typeset ret new_owner

	if [[ $user == "" || $node == "" ]]; then
		log_fail "user, node are not defined."
	fi

	chgusr_exec $user $CHOWN $expect_owner $node ; ret=$?
	new_owner=$(get_owner $node)

	if [[ $new_owner != $old_owner ]]; then
		$TAR xpf $TESTDIR/$ARCHIVEFILE
	fi

	if [[ $ret -eq 0 ]]; then
		if [[ $new_owner != $expect_owner ]]; then
			log_note "Owner not changed as expected " \
				"($old_owner|$new_owner|$expect_owner), " \
				"but return code is $ret."
			return 1
		fi
	elif [[ $ret -ne 0 && $new_owner != $old_owner ]]; then
		log_note "Owner changed ($old_owner|$new_owner), " \
			"but return code is $ret."
		return 2
	fi
		
	return $ret
}

function operate_node_group #user node old_group expect_group
{
	typeset user=$1
	typeset node=$2
	typeset old_group=$3
	typeset expect_group=$4
	typeset ret new_group

	if [[ $user == "" || $node == "" ]]; then
		log_fail "user, node are not defined."
	fi

	chgusr_exec $user $CHGRP $expect_group $node ; ret=$?
	new_group=$(get_group $node)

	if [[ $new_group != $old_group ]]; then
		$TAR xpf $TESTDIR/$ARCHIVEFILE
	fi

	if [[ $ret -eq 0 ]]; then
		if [[ $new_group != $expect_group ]]; then
			log_note "Group not changed as expected " \
				"($old_group|$new_group|$expect_group), " \
				"but return code is $ret."
			return 1
		fi
	elif [[ $ret -ne 0 && $new_group != $old_group ]]; then
		log_note "Group changed ($old_group|$new_group), " \
			"but return code is $ret."
		return 2
	fi
		
	return $ret
}

function logname #acl_target user old new
{
	typeset acl_target=$1
	typeset user=$2
	typeset old=$3
	typeset new=$4
	typeset ret="log_mustnot"

	# To super user, read and write deny permission was override.
	if [[ $user == root ]]; then
		ret="log_must"
	elif [[ $user == $new ]] ; then
		if [[ $user == $old || $acl_target == *:allow ]]; then
			ret="log_must"
		fi
	fi

	print $ret
}

function check_chmod_results #node flag acl_target g_usr o_usr
{
	typeset node=$1
	typeset flag=$2
	typeset acl_target=$2:$3
	typeset g_usr=$4
	typeset o_usr=$5
	typeset log old_owner old_group new_owner new_group

	old_owner=$(get_owner $node)
	old_group=$(get_group $node)

	if [[ $flag == "owner@" || $flag == "everyone@" ]]; then
		for new_owner in $ZFS_ACL_CUR_USER "nobody"; do 
			new_group=$(get_user_group $new_owner)

			log=$(logname $acl_target $ZFS_ACL_CUR_USER \
				$old_owner $new_owner)

			$log operate_node_owner $ZFS_ACL_CUR_USER $node \
				$old_owner $new_owner

			$log operate_node_group $ZFS_ACL_CUR_USER $node \
				$old_group $new_group
		done
	fi
	if [[ $flag == "group@" || $flag == "everyone@" ]]; then
		for new_owner in $g_usr "nobody"; do 
			new_group=$(get_user_group $new_owner)

			log=$(logname $acl_target $g_usr $old_owner \
				$new_owner)

			$log operate_node_owner $g_usr $node \
				$old_owner $new_owner

			$log operate_node_group $g_usr \
				$node $old_group $new_group
		done
	fi
	if [[ $flag == "everyone@" ]]; then
		for new_owner in $g_usr "nobody"; do 
			new_group=$(get_user_group $new_owner)

			log=$(logname $acl_target $o_usr $old_owner \
				$new_owner)

			$log operate_node_owner $o_usr $node \
				$old_owner $new_owner

			$log operate_node_group $o_usr $node \
				$old_group $new_group
		done
	fi
}

function test_chmod_basic_access #node g_usr o_usr
{
	typeset node=${1%/}
	typeset g_usr=$2
	typeset o_usr=$3
	typeset flag acl_p acl_t parent 

	parent=${node%/*}

	for flag in ${a_flag[@]}; do
		for acl_t in "${a_access[@]}"; do
			log_must usr_exec $CHMOD A+$flag:$acl_t $node

			$TAR cpf $TESTDIR/$ARCHIVEFILE basedir

			check_chmod_results "$node" "$flag" \
				"$acl_t" "$g_usr" "$o_usr"

			log_must usr_exec $CHMOD A0- $node
		done
	done
}

function setup_test_files #base_node user group
{
	typeset base_node=$1
	typeset user=$2
	typeset group=$3

	cleanup_test_files $base_node

	log_must $MKDIR -p $base_node
	log_must $CHOWN $user:$group $base_node

	log_must set_cur_usr $user

	# Prepare all files/sub-dirs for testing.
 
	file0=$base_node/testfile_rm

	dir0=$base_node/testdir_rm

	log_must usr_exec $TOUCH $file0
	log_must usr_exec $CHMOD 444 $file0

	log_must usr_exec $MKDIR -p $dir0
	log_must usr_exec $CHMOD 444 $dir0

	log_must usr_exec $CHMOD 555 $base_node
	return 0	
}

function cleanup_test_files #base_node
{
	typeset base_node=$1

	if [[ -d $base_node ]]; then
		log_must $RM -rf $base_node
	elif [[ -e $base_node ]]; then
		log_must $RM -f $base_node
	fi

	return 0
}

typeset cwd=$PWD
typeset ARCHIVEFILE=archive.tar
 
test_requires ZFS_ACL

typeset -i i=0
typeset -i j=0
typeset target
cd $TESTDIR
while (( i < ${#users[@]} )); do
	setup_test_files $TESTDIR/basedir ${users[i]} ${users[((i+1))]}

	j=0
	while (( j < 1 )); do
		eval target=\$file$j	
		test_chmod_basic_access $target \
			"${users[((i+2))]}" "${users[((i+3))]}"

		eval target=\$dir$j	
		test_chmod_basic_access $target \
			"${users[((i+2))]}" "${users[((i+3))]}"

		(( j = j + 1 ))
	done
	
	(( i += 4 ))
done

log_pass "Verify that the chown/chgrp could take owner/group " \
	"while permission is granted."
