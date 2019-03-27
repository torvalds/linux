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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)cifs_attr_002_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib
. $STF_SUITE/tests/acl/cifs/cifs.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: cifs_attr_002_pos
#
# DESCRIPTION:
#	Verify the user with PRIV_FILE_FLAG_SET/PRIV_FILE_FLAG_CLEAR
#	could set/clear BSD'ish attributes.
#	(Immutable, nounlink, and appendonly)
#	
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Grant user has PRIV_FILE_FLAG_SET/PRIV_FILE_FLAG_CLEAR separately.
#	4. Verify set/clear BSD'ish attributes should succeed.
#	
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-11-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

if ! cifs_supported ; then
	log_unsupported "CIFS not supported on current system."
fi

test_requires ZFS_ACL ZFS_XATTR

function cleanup
{
	if [[ -n $gobject ]]; then
		destroy_object $gobject
	fi

	for fs in $TESTPOOL/$TESTFS $TESTPOOL ; do
		mtpt=$(get_prop mountpoint $fs)
		log_must $RM -rf $mtpt/file.* $mtpt/dir.*
	done
}

#
# Set the special attribute to the given node
#
# $1: The given node (file/dir)
# $2: The special attribute to be set
# $3: Execute username
#
function set_attribute
{
	typeset object=$1
	typeset attr=$2
	typeset user=$3
	typeset ret=0

	if [[ -z $object ]]; then
		log_fail "Object not defined."
	fi

	if [[ -z $attr ]]; then
		attr="uiadm"
		if [[ -f $object ]]; then
			attr="${attr}q"
		fi
	fi

	if [[ -n $user ]]; then
		$RUNWATTR -u $user -p =basic${priv_mod} \
			"$CHMOD S+c${attr} $object"
		ret=$?
	else
		$CHMOD S+c${attr} $object
		ret=$?
	fi

	return $ret
}

#
# Clear the special attribute to the given node
#
# $1: The given node (file/dir)
# $2: The special attribute to be cleared
# $3: Execute username
#
function clear_attribute
{
	typeset object=$1
	typeset attr=$2
	typeset user=$3
	typeset ret=0

	if [[ -z $object ]]; then
		log_fail "Object($object) not defined."
	fi

	if [[ -z $attr ]]; then
		attr="uiadm"
		if [[ -f $object ]]; then
			attr="${attr}q"
		fi
	fi

	if [[ -n $user ]]; then
		$RUNWATTR -u $user -p =basic${priv_mod} \
			"$CHMOD S-c${attr} $object"
		ret=$?
	else
		$CHMOD S-c${attr} $object
		ret=$?
	fi

	return $ret
}

#
# Grant the privset to the given user
#
# $1: The given user
# $2: The given privset
#
function grant_priv
{
	typeset user=$1
	typeset priv=$2

	if [[ -z $user || -z $priv ]]; then
		log_fail "User($user), Priv($priv) not defined."
	fi
	priv_mod=",$priv"
	return $?
}

#
# Revoke the all additional privset from the given user
#
# $1: The given user
#
function revoke_priv
{
	typeset user=$1
	
	if [[ -z $user ]]; then
		log_fail "User not defined."
	fi
	priv_mod=
	return $?
}

#
# Invoke the function and verify whether its return code as expected
#
# $1: Function be invoked
# $2: The given node (file/dir)
# $3: Execute user
# $4: Option
#
function verify_op
{
	typeset func=$1
	typeset object=$2
	typeset opt=$3
	typeset user=$4
	typeset expect="log_mustnot"

	if [[ -z $func || -z $object ]]; then
		log_fail "Func($func), Object($object) not defined."
	fi

	# If user has PRIV_FILE_FLAG_SET, it could permit to set_attribute,
	# And If has PRIV_FILE_FLAG_CLEAR, it could permit to clear_attribute,
	# otherwise log_mustnot.
	if [[ -z $user || $user == "root" ]] || \
		[[ $priv_mod == *"file_flag_set"* ]] || \
		[[ $priv_mod == *"all"* ]] ; then
			expect="log_must"
	fi
	if [[ -d $object ]] && \
		[[ $opt == *"q"* ]] ; then
		expect="log_mustnot"
	fi	
				
	if [[ $func == clear_attribute ]]; then
		if [[ $expect == "log_mustnot" ]]; then
			expect="log_must"
		elif [[ -z $user || $user == "root" ]] || \
			[[ $priv_mod == *"all"* ]] ; then
			expect="log_must"
		else
			expect="log_mustnot"
		fi
	fi

	$expect $func $object $opt $user
}
			
log_assert "Verify set/clear BSD'ish attributes will succeed while user has " \
	"PRIV_FILE_FLAG_SET/PRIV_FILE_FLAG_CLEAR privilege"
log_onexit cleanup

file="file.0"
dir="dir.0"
FLAGOPTIONS="u i a d q m"

typeset gobject
for fs in $TESTPOOL $TESTPOOL/$TESTFS ; do
	mtpt=$(get_prop mountpoint $fs)
	for owner in root $ZFS_ACL_STAFF1 ; do

		create_object "file" $mtpt/$file $owner
		create_object "dir" $mtpt/$dir $owner

		for object in $mtpt/$file $mtpt/$dir ; do
			gobject=$object
			for user in root $ZFS_ACL_STAFF2 ; do
				log_must grant_priv $user file_flag_set
				for opt in $FLAGOPTIONS ; do
					verify_op set_attribute \
						$object $opt $user
					verify_op clear_attribute \
						$object $opt $user
				done
				log_must revoke_priv $user

				log_must grant_priv $user all
				for opt in $FLAGOPTIONS ; do
					verify_op set_attribute \
						$object $opt $user
					verify_op clear_attribute \
						$object $opt $user
				done
				log_must revoke_priv $user
			done
		done
		destroy_object $mtpt/$file $mtpt/$dir
	done
done

log_pass "Set/Clear BSD'ish attributes succeed while user has " \
	"PRIV_FILE_FLAG_SET/PRIV_FILE_FLAG_CLEAR privilege"
