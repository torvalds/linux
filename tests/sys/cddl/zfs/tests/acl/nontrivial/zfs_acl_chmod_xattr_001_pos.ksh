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
# ident	"@(#)zfs_acl_chmod_xattr_001_pos.ksh	1.4	09/01/13 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_xattr_001_pos
#
# DESCRIPTION:
#	Verify that the read_xattr/write_xattr for 
#	owner/group/everyone are correct.
#
# STRATEGY:
# 1. Create file and  directory in zfs filesystem
# 2. Set special read_xattr ACE to the file and directory
# 3. Try to list the extended attributes of the file and directory
# 4. Set special write_xattr ACE to the file and directory
# 5. Try to add new extended attributes to the file and directory
# 6. Verify above operation is successful.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-11-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	cd $cwd

	cleanup_test_files $TESTDIR/basedir

	if [[ -e $TESTDIR/$ARCHIVEFILE ]]; then
		log_must $RM -f $TESTDIR/$ARCHIVEFILE
	fi

	return 0
}

#	owner@	group	group_users		other_users
set -A users \
	"root"	"root"	"$ZFS_ACL_ADMIN" 	"$ZFS_ACL_OTHER1" \
	"$ZFS_ACL_STAFF1"	"$ZFS_ACL_STAFF_GROUP"	"$ZFS_ACL_STAFF2" 	"$ZFS_ACL_OTHER1"

set -A a_access \
	"read_xattr:allow" \
	"read_xattr:deny" \
	"write_xattr:allow" \
	"write_xattr:deny"

set -A a_flag "owner@" "group@" "everyone@"

MYTESTFILE=$STF_SUITE/include/default.cfg

log_assert "Verify that the permission of read_xattr/write_xattr for " \
	"owner/group/everyone are correct."
log_onexit cleanup

function operate_node #user node acl
{
	typeset user=$1
	typeset node=$2
	typeset acl_t=$3
	typeset ret

	if [[ $user == "" || $node == "" ]]; then
		log_fail "user, node are not defined."
	fi

	if [[ $acl_t == *read_xattr* ]]; then
		chgusr_exec $user $RUNAT $node $LS > /dev/null 2>&1; ret=$?
	elif [[ $acl_t == *write_xattr* ]]; then
		chgusr_exec $user $RUNAT $node $CP $MYTESTFILE attr.1 ; ret=$?

		if [[ $ret -eq 0 ]]; then
			log_must cleanup_test_files $TESTDIR/basedir
			log_must $TAR xpf@ $TESTDIR/$ARCHIVEFILE
		fi
	fi

	return $ret
}

function logname #acl_target user
{
	typeset acl_target=$1
	typeset user=$2
	typeset ret="log_mustnot"

	# To super user, read and write deny permission was override.
	if [[ $user == root || $acl_target == *:allow ]] then
		ret="log_must"
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
	typeset log

	if [[ $flag == "owner@" || $flag == "everyone@" ]]; then
		log=$(logname $acl_target $ZFS_ACL_CUR_USER)
		$log operate_node $ZFS_ACL_CUR_USER $node $acl_target
	fi
	if [[ $flag == "group@" || $flag == "everyone@" ]]; then
		log=$(logname $acl_target $g_usr)
		$log operate_node $g_usr $node $acl_target
	fi
	if [[ $flag == "everyone@" ]]; then
		log=$(logname $acl_target $o_usr)
		$log operate_node $o_usr $node $acl_target
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

			log_must $TAR cpf@ $TESTDIR/$ARCHIVEFILE basedir

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

	log_must usr_exec $RUNAT $file0 $CP $MYTESTFILE attr.0

	log_must usr_exec $MKDIR -p $dir0
	log_must usr_exec $CHMOD 555 $dir0

	log_must usr_exec $RUNAT $dir0 $CP $MYTESTFILE attr.0

	log_must usr_exec $CHMOD 777 $base_node
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

test_requires RUNAT ZFS_ACL ZFS_XATTR

typeset -i i=0
typeset -i j=0
typeset target

while (( i < ${#users[@]} )); do
	setup_test_files $TESTDIR/basedir ${users[i]} ${users[((i+1))]}
	cd $TESTDIR

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

log_pass "Verify that the permission of read_xattr/write_xattr for " \
	"owner/group/everyone are correct."
