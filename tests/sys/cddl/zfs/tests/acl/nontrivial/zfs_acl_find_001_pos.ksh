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
# ident	"@(#)zfs_acl_find_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_find_001_pos
#
# DESCRIPTION:
# Verify that '$FIND' command with '-ls' and '-acl' options supports ZFS ACL 
#
# STRATEGY:
# 1. Create 5 files and 5 directories in zfs filesystem
# 2. Select a file or directory and add a few ACEs to it 
# 3. Use $FIND -ls to check the "+" existen only with the selected file or 
#    directory
# 4. Use $FIND -acl to check only the selected file/directory in the list
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
	[[ -d $TESTDIR ]] && $RM -rf $TESTDIR/*
	(( ${#cwd} != 0 )) && cd $cwd
	(( ${#mask} != 0 )) && $UMASK $mask
}

function find_ls_acl #<opt> <obj>
{
	typeset opt=$1 # -ls or -acl
	typeset obj=$2
	typeset rst_str=""

	if [[ $opt == "ls" ]]; then
		rst_str=`$FIND . -ls | $GREP "+" | $AWK '{print $11}'`
	else
		rst_str=`$FIND . -acl`
	fi

	if [[ $rst_str == "./$obj" ]]; then 
		return 0
	else
		return 1
	fi
}

log_assert "Verify that '$FIND' command supports ZFS ACLs."
log_onexit cleanup

test_requires ZFS_ACL

set -A ops " A+everyone@:read_data:allow" \
	" A+owner@:write_data:allow" 

f_base=testfile.${TESTCASE_ID} # Base file name for tested files
d_base=testdir.${TESTCASE_ID} # Base directory name for tested directory
cwd=$PWD
mask=`$UMASK`

log_note "Create five files and directories in the zfs filesystem. "
cd $TESTDIR
$UMASK 0777
typeset -i i=0
while (( i < 5 ))
do
	log_must $TOUCH ${f_base}.$i
	log_must $MKDIR ${d_base}.$i

	(( i = i + 1 ))
done

for obj in ${f_base}.3 ${d_base}.3
do
	i=0
	while (( i < ${#ops[*]} ))
	do
		log_must $CHMOD ${ops[i]} $obj

		(( i = i + 1 ))
	done

	for opt in "ls" "acl"
	do
		log_must find_ls_acl $opt $obj
	done

	log_note "Check the file access permission according to the added ACEs" 
	if [[ ! -r $obj || ! -w $obj ]]; then
		log_fail "The added ACEs for $obj cannot be represented in " \
			"mode."
	fi
	
	log_note "Remove the added ACEs from ACL."
	i=0
	while (( i < ${#ops[*]} ))
	do
		log_must $CHMOD A0- $obj
		
		(( i = i + 1 ))
	done
done

log_pass "'$FIND' command succeeds to support ZFS ACLs."
