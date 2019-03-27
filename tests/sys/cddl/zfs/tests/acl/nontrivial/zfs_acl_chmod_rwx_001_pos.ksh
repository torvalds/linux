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
# ident	"@(#)zfs_acl_chmod_rwx_001_pos.ksh	1.3	09/01/13 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_rwx_001_pos
#
# DESCRIPTION:
#	chmod A{+|-|=} have the correct behaviour to the ACL list. 	
#
# STRATEGY:
#	1. loop check root and non-root users
#	2. chmod file or dir with specified options
#	3. get ACE after behaviours of chmod
#	4. compare specified ACE and excpect ACE
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-30)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "chmod A{+|-|=} have the correct behaviour to the ACL list."
log_onexit cleanup

typeset -i trival_count=6 head=0 mid end
((mid = RANDOM % $trival_count))
((end = trival_count - 1))

opts="+ - ="
nums="$head $mid $end"
set -A file_ACEs \
	"user:$ZFS_ACL_STAFF1:read_data:allow" \
	"user:$ZFS_ACL_STAFF2:write_data:allow" \
	"user:$ZFS_ACL_OTHER1:execute:allow"
set -A dir_ACEs \
	"user:$ZFS_ACL_STAFF1:list_directory/read_data:allow" \
	"user:$ZFS_ACL_STAFF2:add_file/write_data:allow" \
	"user:$ZFS_ACL_OTHER1:execute:allow"

function test_chmod_ACE_list #$opt $num $ace-spec $node
{
	typeset opt=A$2$1
	typeset -i num=$2
	typeset ace=$3
	typeset node=$4
	typeset -i expect_count=0

	# Get expect ACE count
	case $opt in
		A[0-9]*+) (( expect_count = trival_count + 1 )) ;;
		A[0-9]*-) (( expect_count = trival_count - 1 )) ;;
		A[0-9]*=) (( expect_count = trival_count )) ;;
		*) log_fail "Error option: '$opt'" ;;
	esac

	# Invoke chmod A[number]{+|-|=}<acl-specification> file|dir
	if [[ $opt == A[0-9]*+ || $opt == A[0-9]*= ]]; then
		log_must usr_exec $CHMOD "$opt$ace" "$node"
	else
		log_must usr_exec $CHMOD "$opt" "$node"
	fi

	# Get the current ACE count and specified ACE
	typeset cur_ace cur_count
	cur_ace=$(get_ACE $node $num)
	cur_count=$(count_ACE $node)

	# Compare with expected results
	if [[ $opt == A[0-9]*+ || $opt == A[0-9]*= ]]; then
		if [[ "$num:$ace" != "$cur_ace" ]]; then
			log_fail "FAIL: $CHMOD $opt$ace $node"
		fi
	fi
	if [[ "$expect_count" != "$cur_count" ]]; then
		log_fail "FAIL: '$expect_count' != '$cur_count'"
	fi
}

test_requires ZFS_ACL

for user in root $ZFS_ACL_STAFF1 $ZFS_ACL_OTHER1; do
	log_must set_cur_usr $user

	for opt in $opts; do
		for num in $nums; do
			for ace in $file_ACEs; do
				ls -l $TESTDIR
				log_must usr_exec $TOUCH $testfile
				test_chmod_ACE_list $opt $num $ace $testfile
				log_must $RM -f $testfile
			done
			for ace in $dir_ACEs; do
				ls -l $TESTDIR
				log_must usr_exec $MKDIR -p $testdir
				test_chmod_ACE_list $opt $num $ace $testdir
				log_must $RM -rf $testdir
			done
		done
	done
done	

log_pass "chmod A{+|-|=} behave to the ACL list passed."
