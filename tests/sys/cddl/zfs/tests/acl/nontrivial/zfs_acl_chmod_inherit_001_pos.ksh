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
# ident	"@(#)zfs_acl_chmod_inherit_001_pos.ksh	1.5	09/05/19 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_inherit_001_pos
#
# DESCRIPTION:
#	Verify chmod have correct behaviour to directory and file when setting
#	different inherit strategy to them.
#	
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Separately chmod basedir with different inherite options.
#	4. Then create nested directories and files like the following.
#	
#                                                   _ odir4
#                                                  |_ ofile4
#                                         _ odir3 _|
#                                        |_ ofile3
#                               _ odir1 _|
#                              |_ ofile2
#                     basefile |
#          chmod -->  basedir -| 
#                              |_ nfile1
#                              |_ ndir1 _ 
#                                        |_ nfile2
#                                        |_ ndir2 _
#                                                  |_ nfile3
#                                                  |_ ndir3
#
#	5. Verify each directories and files have the correct access control
#	   capability.
#	
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-11-15)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if [[ -f $basefile ]]; then
		log_must $RM -f $basefile
	fi
	if [[ -d $basedir ]]; then
		log_must $RM -rf $basedir
	fi
}

log_assert "Verify chmod have correct behaviour to directory and file when " \
	"setting different inherit strategies to them."
log_onexit cleanup

# Define inherit flag
set -A object_flag file_inherit dir_inherit file_inherit/dir_inherit
set -A strategy_flag "" inherit_only no_propagate inherit_only/no_propagate

# Defile the based directory and file
basedir=$TESTDIR/basedir;  basefile=$TESTDIR/basefile

test_requires ZFS_ACL

# Define the existed files and directories before chmod
odir1=$basedir/odir1; odir2=$odir1/odir2; odir3=$odir2/odir3
ofile1=$basedir/ofile1; ofile2=$odir1/ofile2; ofile3=$odir2/ofile3

# Define the files and directories will be created after chmod
ndir1=$basedir/ndir1; ndir2=$ndir1/ndir2; ndir3=$ndir2/ndir3
nfile1=$basedir/nfile1; nfile2=$ndir1/nfile2; nfile3=$ndir2/nfile3

# Verify all the node have expected correct access control
allnodes="$basedir $ndir1 $ndir2 $ndir3 $nfile1 $nfile2 $nfile3"
allnodes="$allnodes $odir1 $odir2 $odir3 $ofile1 $ofile2 $ofile3"

#
# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
#
function verify_inherit #<object> [strategy]
{
	# Define the nodes which will be affected by inherit.
	typeset inherit_nodes
	typeset obj=$1
	typeset str=$2

	log_must usr_exec $MKDIR -p $ndir3
	log_must usr_exec $TOUCH $nfile1 $nfile2 $nfile3

	# Except for inherit_only, the basedir was affected always.
	if [[ $str != *"inherit_only"* ]]; then
		inherit_nodes="$inherit_nodes $basedir"
	fi
	# Get the files which inherited ACE.
	if [[ $obj == *"file_inherit"* ]]; then
		inherit_nodes="$inherit_nodes $nfile1"

		if [[ $str != *"no_propagate"* ]]; then
			inherit_nodes="$inherit_nodes $nfile2 $nfile3"
		fi
	fi
	# Get the directores which inherited ACE.
	if [[ $obj == *"dir_inherit"* ]]; then
		inherit_nodes="$inherit_nodes $ndir1"

		if [[ $str != *"no_propagate"* ]]; then
			inherit_nodes="$inherit_nodes $ndir2 $ndir3"
		fi
	fi
	
	for node in $allnodes; do
		if [[ " $inherit_nodes " == *" $node "* ]]; then
			log_mustnot chgusr_exec $ZFS_ACL_OTHER1 $LS -vd $node \
				> /dev/null 2>&1
		else
			log_must chgusr_exec $ZFS_ACL_OTHER1 $LS -vd $node \
				> /dev/null 2>&1
		fi
	done
}

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	for obj in "${object_flag[@]}"; do
		for str in "${strategy_flag[@]}"; do
			typeset inh_opt=$obj
			(( ${#str} != 0 )) && inh_opt=$inh_opt/$str
			aclspec="A+user:$ZFS_ACL_OTHER1:read_acl:$inh_opt:deny"

			log_must usr_exec $MKDIR $basedir
			log_must usr_exec $TOUCH $basefile 
			log_must usr_exec $MKDIR -p $odir3
			log_must usr_exec $TOUCH $ofile1 $ofile2 $ofile3

			#
			# Inherit flag can only be placed on a directory,
			# otherwise it will fail.
			#
			log_must usr_exec $CHMOD $aclspec $basefile

			#
			# Place on a directory should succeed.
			#
			log_must usr_exec $CHMOD $aclspec $basedir
			
			verify_inherit $obj $str
			
			log_must usr_exec $RM -rf $basefile $basedir
		done
	done
done

log_pass "Verify chmod inherit behaviour passed."
