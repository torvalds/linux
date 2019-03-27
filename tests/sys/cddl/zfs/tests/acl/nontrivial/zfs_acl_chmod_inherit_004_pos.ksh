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
# ident	"@(#)zfs_acl_chmod_inherit_004_pos.ksh	1.1	09/05/19 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_inherit_004_pos
#
# DESCRIPTION:
#	Verify aclinherit=passthrough-x will inherit the 'x' bits while mode request.
#	
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Set aclinherit=passthrough-x
#	4. Verify only passthrough-x will inherit the 'x' bits while mode request.
#	
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if [[ -d $basedir ]]; then
		log_must $RM -rf $basedir
	fi
}

$ZPOOL upgrade -v | $GREP "passthrough\-x aclinherit support" > /dev/null 2>&1
if (( $? != 0 )) ; then
	log_unsupported "passthrough-x aclinherit not supported."
fi

log_assert "Verify aclinherit=passthrough-x will inherit the 'x' bits while mode request."
log_onexit cleanup

set -A aces "owner@:read_data/write_data/add_subdirectory/append_data/execute:dir_inherit/inherit_only:allow" \
	"owner@:read_data/write_data/add_subdirectory/append_data/execute::allow" \
	"group@:add_subdirectory/append_data/execute:dir_inherit/inherit_only:allow" \
	"group@:add_subdirectory/append_data/execute::allow" \
	"everyone@:add_subdirectory/append_data/execute:dir_inherit/inherit_only:allow" \
	"everyone@:add_subdirectory/append_data/execute::allow" \
	"owner@:read_data/write_data/add_subdirectory/append_data/execute:file_inherit/inherit_only:allow" \
	"group@:read_data/add_subdirectory/append_data/execute:file_inherit/inherit_only:allow" \
	"everyone@:read_data/add_subdirectory/append_data/execute:file_inherit/inherit_only:allow"

# Defile the based directory and file
basedir=$TESTDIR/basedir

test_requires ZFS_ACL

#
# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
#
function verify_inherit # <object>
{
	typeset obj=$1

	# Define the files and directories will be created after chmod
	ndir1=$obj/ndir1; ndir2=$ndir1/ndir2
	nfile1=$ndir1/nfile1.c; nfile2=$ndir1/nfile2
	
	log_must usr_exec $MKDIR -p $ndir1

	typeset -i i=0
	while (( i < ${#aces[*]} )) ; do
		if (( i < 6 )) ; then 
			log_must usr_exec $CHMOD A$i=${aces[i]} $ndir1
		else
			log_must usr_exec $CHMOD A$i+${aces[i]} $ndir1
		fi
		(( i = i + 1 ))
	done
	log_must usr_exec $MKDIR -p $ndir2
	log_must usr_exec $TOUCH $nfile1

	$CAT > $nfile1 <<EOF
#include <stdlib.h>
#include <stdio.h>
int main()
{ return 0; }
EOF

	mode=$(get_mode $ndir2)
	if [[ $mode != "drwx--x--x"* ]] ; then
		log_fail "Unexpect mode of $ndir2, expect: drwx--x--x, current: $mode"
	fi

	mode=$(get_mode $nfile1)
	if [[ $mode != "-rw-r--r--"* ]] ; then
		log_fail "Unexpect mode of $nfile1, expect: -rw-r--r--, current: $mode"
	fi

	if [[ -x /usr/sfw/bin/gcc ]] ; then
		log_must /usr/sfw/bin/gcc -o $nfile2 $nfile1
		mode=$(get_mode $nfile2)
		if [[ $mode != "-rwxr-xr-x"* ]] ; then
			log_fail "Unexpect mode of $nfile2, expect: -rwxr-xr-x, current: $mode"
		fi
	fi
}

#
# Set aclmode=passthrough to make sure
# the acl will not change during chmod.
# A general testing should verify the combination of 
# aclmode/aclinherit works well,
# here we just simple test them separately.
#

log_must $ZFS set aclmode=passthrough $TESTPOOL/$TESTFS
log_must $ZFS set aclinherit=passthrough-x $TESTPOOL/$TESTFS

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	verify_inherit $basedir

	cleanup
done

log_pass "Verify aclinherit=passthrough-x will inherit the 'x' bits while mode request."
