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
# ident	"@(#)zfs_acl_chmod_001_pos.ksh	1.5	09/01/13 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_001_pos
#
# DESCRIPTION:
#	Verify chmod permission settings on files and directories, as both root
#	and non-root users.
#
# STRATEGY:
#	1. Loop root and $ZFS_ACL_STAFF1 as root and non-root users.
#	2. Create test file and directory in zfs filesystem.
#	3. Execute 'chmod' with specified options.
#	4. Check 'ls -l' output and compare with expect results.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

# 	"init_map" "options" "expect_map"
set -A argv \
	"000" "a+rw"	"rw-rw-rw-" 	"000" "a+rwx"	"rwxrwxrwx" \
	"000" "u+xr"	"r-x------"	"000" "gu-xw"	"---------" \
	"644" "a-r"	"-w-------"	"644" "augo-x"	"rw-r--r--" \
	"644" "=x"	"--x--x--x"	"644" "u-rw"	"---r--r--" \
	"644" "uo+x"	"rwxr--r-x"	"644" "ga-wr"	"---------" \
	"777" "augo+x"	"rwxrwxrwx"	"777" "go-xr"	"rwx-w--w-" \
	"777" "o-wx"	"rwxrwxr--" 	"777" "ou-rx"	"-w-rwx-w-" \
	"777" "a+rwx"	"rwxrwxrwx"	"777" "u=rw"	"rw-rwxrwx" \
	"000" "123"	"--x-w--wx"	"000" "412"	"r----x-w-" \
	"231" "562"	"r-xrw--w-"	"712" "000"	"---------" \
	"777" "121"	"--x-w---x"	"123" "775"	"rwxrwxr-x"

log_assert " Verify chmod permission settings on files and directories"
log_onexit cleanup

#
# Verify file or directory have correct map after chmod 
#
# $1 file or directory
#
function test_chmod_mapping #<file-dir>
{
	typeset node=$1
	typeset -i i=0

	while (( i < ${#argv[@]} )); do
		usr_exec $CHMOD ${argv[i]} $node
		if (($? != 0)); then
			log_note "usr_exec $CHMOD ${argv[i]} $node"
			return 1
		fi

		usr_exec $CHMOD ${argv[((i + 1))]} $node
		if (($? != 0)); then
			log_note "usr_exec $CHMOD ${argv[((i + 1))]} $node"
			return 1
		fi

		typeset mode
		mode=$(get_mode ${node})

		if [[ $mode != "-${argv[((i + 2))]}"* && \
			$mode != "d${argv[((i + 2))]}"* ]]
		then
			log_fail "FAIL: '${argv[i]}' '${argv[((i + 1))]}' \
				'${argv[((i + 2))]}'"
		fi

		(( i += 3 ))
	done

	return 0
}

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	# Test file
	log_must usr_exec $TOUCH $testfile
	log_must test_chmod_mapping $testfile

	if [ "$ZFS_ACL" != "" ] ; then
		log_must $CHMOD A+user:$ZFS_ACL_STAFF2:write_acl:allow $testfile
	fi
	log_must set_cur_usr $ZFS_ACL_STAFF2

	# Test directory
	log_must usr_exec $MKDIR $testdir
	log_must test_chmod_mapping $testdir

	if [ "$ZFS_ACL" != "" ] ; then
		# Grant privileges of write_acl and retest the chmod commands.
		acl="user:$ZFS_ACL_STAFF2:write_acl:allow"
		log_must usr_exec $CHMOD A+${acl} $testfile
		log_must usr_exec $CHMOD A+${acl} $testdir

		log_must set_cur_usr $ZFS_ACL_STAFF2
		log_must test_chmod_mapping $testfile
		log_must test_chmod_mapping $testdir
	fi

	log_must set_cur_usr $user

	log_must usr_exec $RM $testfile
	log_must usr_exec $RM -rf $testdir
done

log_pass "Setting permissions using 'chmod' completed successfully."
