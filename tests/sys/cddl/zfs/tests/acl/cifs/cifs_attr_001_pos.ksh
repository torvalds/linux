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
# ident	"@(#)cifs_attr_001_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib
. $STF_SUITE/tests/acl/cifs/cifs.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: cifs_attr_001_pos
#
# DESCRIPTION:
#	Verify the user with write_attributes permission or
#	PRIV_FILE_OWNER privilege could set/clear DOS attributes.
#	(Readonly, Hidden, Archive, System)
#	
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Grant user has write_attributes permission or
#		PRIV_FILE_OWNER privilege
#	4. Verify set/clear DOS attributes should succeed.
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

verify_runnable "both"

if ! cifs_supported ; then
	log_unsupported "CIFS not supported on current system."
fi

test_requires ZFS_ACL ZFS_XATTR

function cleanup
{
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
	typeset attr=${2:-AHRS}
	typeset user=$3
	typeset ret=0

	if [[ -z $object ]]; then
		log_fail "Object not defined."
	fi

	if [[ -n $user ]]; then
		$RUNWATTR -u $user "$CHMOD S+c${attr} $object"
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
	typeset attr=${2:-AHRS}
	typeset user=$3
	typeset ret=0

	if [[ -z $object ]]; then
		log_fail "Object not defined."
	fi

	if [[ -n $user ]]; then
		$RUNWATTR -u $user "$CHMOD S-c${attr} $object"
		ret=$?
	else
        	$CHMOD S-c${attr} $object
		ret=$?
	fi

	return $ret
}

#
# Grant the ace of write_attributes to the given user
#
# $1: The given user
# $2: The given node (file/dir)
#
function grant_attr
{
        typeset user=$1
        typeset object=$2

	if [[ -z $user || -z $object ]]; then
		log_fail "User($user), Object($object) not defined."
	fi

	# To increase the coverage, here we set 'deny' against 
	# superuser and owner.
	# Only grant the user explicitly while it's not root neither owner.

        if [[ $user == "root" ]]; then
                log_must chmod A+user:root:write_attributes:deny $object
        elif [[ $user == $(get_owner $object) ]]; then
                if (( ( RANDOM % 2 ) == 0 )); then
                        log_must chmod A+owner@:write_attributes:deny $object
                else
                        log_must chmod A+user:$user:write_attributes:deny \
				$object
                fi
        else
                log_must chmod A+user:$user:write_attributes:allow $object
        fi
        attr_mod="write_attributes"
}

#
# Revoke the ace of write_attributes from the given user
#
# $1: The given user
# $2: The given node (file/dir)
#
function revoke_attr
{
        typeset user=$1
        typeset object=$2

	if [[ -z $user || -z $object ]]; then
		log_fail "User($user), Object($object) not defined."
	fi

        log_must chmod A0- $object
        attr_mod=
}

#
# Invoke the function and verify whether its return code as expected
#
# $1: Function be invoked
# $2: The given node (file/dir)
# $3: Execute user
# $4: Option
#
function verify_attr
{
        typeset func=$1
        typeset object=$2
        typeset opt=$3
        typeset user=$4
        typeset expect="log_mustnot"

	if [[ -z $func || -z $object ]]; then
		log_fail "Func($func), Object($object), User($user), \
			Opt($opt) not defined."
	fi

	# If user is superuser or has write_attributes permission or
	# PRIV_FILE_OWNER privilege, it should log_must,
	# otherwise log_mustnot.

        if [[ -z $user ||  $user == "root"  || \
                $user == $(get_owner $object) || \
                 $attr_mod == *"write_attributes"* ]] ; then
                        expect="log_must"
        fi

        $expect $func $object $opt $user
}

log_assert "Verify set/clear DOS attributes will succeed while user has " \
	"write_attributes permission or PRIV_FILE_OWNER privilege"
log_onexit cleanup

file="file.0"
dir="dir.0"
XATTROPTIONS="H S R A"

for fs in $TESTPOOL $TESTPOOL/$TESTFS ; do
	mtpt=$(get_prop mountpoint $fs)
	for owner in root $ZFS_ACL_STAFF1 ; do

		create_object "file" $mtpt/$file $owner
		create_object "dir" $mtpt/$dir $owner

		for object in $mtpt/$file $mtpt/$dir ; do
			for user in root $ZFS_ACL_STAFF2 ; do
				for opt in $XATTROPTIONS ; do
					verify_attr set_attribute \
						$object $opt $user
					verify_attr clear_attribute \
						$object $opt $user
				done
				log_must grant_attr $user $object
				for opt in $XATTROPTIONS ; do
					verify_attr set_attribute \
						$object $opt $user
					verify_attr clear_attribute \
						$object $opt $user
				done
				log_must revoke_attr $user $object
			done
		done
		destroy_object $mtpt/$file $mtpt/$dir
	done
done

log_pass "Set/Clear DOS attributes succeed while user has " \
	"write_attributes permission or PRIV_FILE_OWNER privilege"
