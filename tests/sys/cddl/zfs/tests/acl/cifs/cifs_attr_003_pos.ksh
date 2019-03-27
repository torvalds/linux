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
# ident	"@(#)cifs_attr_003_pos.ksh	1.4	09/05/19 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib
. $STF_SUITE/tests/acl/cifs/cifs.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: cifs_attr_003_pos
#
# DESCRIPTION:
#	Verify the DOS attributes (Readonly, Hidden, Archive, System) 
#	and BSD'ish attributes (Immutable, nounlink, and appendonly) 
#	will provide the proper access limitation as expected.
#
#	Readonly means that the content of a file can't be modified, but
#	timestamps, mode and so on can.
#
#	Archive - Indicates if a file should be included in the next backup
#	of the file system.  ZFS will set this bit whenever a file is
#	modified.
#
#	Hidden and System (ZFS does nothing special with these, other than
#	letting a user/application set them.
#
#	Immutable (The data can't, change nor can mode, ACL, size and so on)
#	The only attribute that can be updated is the access time.
#
#	Nonunlink - Sort of like immutable except that a file/dir can't be
#	removed.
#	This will also effect a rename operation, since that involes a
#	remove.
#
#	Appendonly - File can only be appended to.
#
#	nodump, settable, opaque (These are for the MacOS port) we will
#	allow them to be set, but have no semantics tied to them.
#
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Set the file/dir with each kind of special attribute.
#	4. Verify the access limitation works as expected.
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
#
function set_attribute
{
	typeset object=$1
	typeset attr=$2

	if [[ -z $attr ]]; then
		attr="AHRSadimu"
		if [[ -f $object ]]; then
			attr="${attr}q"
		fi
	fi

	$CHMOD S+c${attr} $object
	return $?
}

#
# Clear the special attribute to the given node
#
# $1: The given node (file/dir)
# $2: The special attribute to be cleared
#
function clear_attribute
{
	typeset object=$1
	typeset attr=$2

	if [[ -z $attr ]]; then
		if is_global_zone ; then
			attr="AHRSadimu"
			if [[ -f $object ]]; then
				attr="${attr}q"
			fi
		else
			attr="AHRS"
		fi
	fi

	$CHMOD S-c${attr} $object
	return $?
}

#
# A wrapper function to call test function according to the given attr
#
# $1: The given node (file/dir)
# $2: The special attribute to be test
#
function test_wrapper
{
	typeset object=$1
	typeset attr=$2

	if [[ -z $object || -z $attr ]]; then
		log_fail "Object($object), Attr($attr) not defined."
	fi

	case $attr in
		R)	func=test_readonly
			;;
		i)	func=test_immutable
			;;
		u)	func=test_nounlink
			;;
		a)	func=test_appendonly
			;;
	esac

	if [[ -n $func ]]; then
		$func $object
	fi
}

#
# Invoke the function and verify whether its return code as expected
#
# $1: Expect value
# $2-$n: Function and args need to be invoked
#
function verify_expect
{
	typeset -i expect=$1
	typeset status

	shift

	"$@" > /dev/null 2>&1
	status=$?
	if  [[ $status -eq 0 ]]; then
		if (( expect != 0 )); then
			log_fail "$@ unexpect return 0"
		fi
	else
		if (( expect == 0 )); then
			log_fail "$@ unexpect return $status"
		fi
	fi
}

#
# Unit testing function against overwrite file
#
# $1: The given file node
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_writefile 
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	if [[ -f $object ]]; then
		verify_expect $expect $CHG_USR_EXEC $user \
			$CP $TESTFILE $object
		verify_expect $expect $CHG_USR_EXEC $user \
			$EVAL "$ECHO '$TESTSTR' > $object"
	fi
}

#
# Unit testing function against write new stuffs into a directory
#
# $1: The given directory node
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_writedir
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	if [[ -d $object ]]; then
		verify_expect $expect $CHG_USR_EXEC $user \
			$CP $TESTFILE $object
		verify_expect $expect $CHG_USR_EXEC $user \
			$MKDIR -p $object/$TESTDIR
	fi
}

function unit_appenddata 
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	if [[ ! -d $object ]]; then
		verify_expect $expect $CHG_USR_EXEC $user \
			$EVAL "$ECHO '$TESTSTR' >> $object"
	fi
}

#
# Unit testing function against delete content from a directory
#
# $1: The given node, dir
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_deletecontent
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	if [[ -d $object ]]; then
		for target in $object/${TESTFILE##*/} $object/$TESTDIR ; do
			if [[ -e $target ]]; then
				verify_expect $expect $CHG_USR_EXEC $user \
					$EVAL "$MV $target $target.new"
				verify_expect $expect $CHG_USR_EXEC $user \
					$EVAL "$ECHO y | $RM -r $target.new"
			fi
		done
	fi
}

#
# Unit testing function against delete a node
#
# $1: The given node, file/dir
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_deletedata
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	verify_expect $expect $CHG_USR_EXEC $user \
		$EVAL "$ECHO y | $RM -r $object"

}

#
# Unit testing function against write xattr to a node
#
# $1: The given node, file/dir
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_writexattr
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	verify_expect $expect $CHG_USR_EXEC $user \
		$RUNAT $object "$CP $TESTFILE $TESTATTR"
	verify_expect $expect $CHG_USR_EXEC $user \
		$EVAL "$RUNAT $object \"$ECHO '$TESTSTR' > $TESTATTR\""
	verify_expect $expect $CHG_USR_EXEC $user \
		$EVAL "$RUNAT $object \"$ECHO '$TESTSTR' >> $TESTATTR\""
	if [[ $expect -eq 0 ]]; then
		verify_expect $expect $CHG_USR_EXEC $user \
			$RUNAT $object "$RM -f $TESTATTR"
	fi
}

#
# Unit testing function against modify accesstime of a node
#
# $1: The given node, file/dir
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_accesstime
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	if [[ -d $object ]]; then
		verify_expect $expect $CHG_USR_EXEC $user $LS $object
	else
		verify_expect $expect $CHG_USR_EXEC $user $CAT $object
	fi
}

#
# Unit testing function against modify updatetime of a node
#
# $1: The given node, file/dir
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_updatetime
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	verify_expect $expect $CHG_USR_EXEC $user $TOUCH $object
	verify_expect $expect $CHG_USR_EXEC $user $TOUCH -a $object
	verify_expect $expect $CHG_USR_EXEC $user $TOUCH -m $object
}

#
# Unit testing function against write acl of a node
#
# $1: The given node, file/dir
# $2: Execute user
# $3: Expect value, default to be zero
#
function unit_writeacl
{
	typeset object=$1
	typeset user=$2
	typeset expect=${3:-0}

	verify_expect $expect $CHG_USR_EXEC $user chmod A+$TESTACL $object
	verify_expect $expect $CHG_USR_EXEC $user chmod A+$TESTACL $object
	verify_expect $expect $CHG_USR_EXEC $user chmod A0- $object
	verify_expect $expect $CHG_USR_EXEC $user chmod A0- $object
	oldmode=$(get_mode $object)
	verify_expect $expect $CHG_USR_EXEC $user chmod $TESTMODE $object
}

#
# Testing function to verify the given node is readonly
#
# $1: The given node, file/dir
#
function test_readonly 
{
	typeset object=$1

	if [[ -z $object ]]; then
		log_fail "Object($object) not defined."
	fi

	log_note "Testing readonly of $object"

	for user in $ZFS_ACL_CUR_USER root $ZFS_ACL_STAFF2; do
		if [[ -d $object ]]; then
			log_must usr_exec chmod \
				A+user:$user:${ace_dir}:allow $object
		else 
			log_must usr_exec chmod \
				A+user:$user:${ace_file}:allow $object
		fi

		log_must set_attribute $object "R"

		unit_writefile $object $user 1
		unit_writedir $object $user
		unit_appenddata $object $user 1

		if [[ -d $object ]]; then
			unit_writexattr $object $user
		else
			unit_writexattr $object $user 1
		fi

		unit_accesstime $object $user
		unit_updatetime $object $user
		unit_writeacl $object $user
		unit_deletecontent $object $user
		unit_deletedata $object $user

		if [[ -d $object ]] ;then
			create_object "dir" $object $ZFS_ACL_CUR_USER
		else
			create_object "file" $object $ZFS_ACL_CUR_USER
		fi
	done
}

#
# Testing function to verify the given node is immutable
#
# $1: The given node, file/dir
#
function test_immutable
{
	typeset object=$1

	if [[ -z $object ]]; then
		log_fail "Object($object) not defined."
	fi

	log_note "Testing immutable of $object"

	for user in $ZFS_ACL_CUR_USER root $ZFS_ACL_STAFF2; do
		if [[ -d $object ]]; then
			log_must usr_exec chmod \
				A+user:$user:${ace_dir}:allow $object
		else 
			log_must usr_exec chmod \
				A+user:$user:${ace_file}:allow $object
		fi
		log_must set_attribute $object "i"

		unit_writefile $object $user 1
		unit_writedir $object $user 1
		unit_appenddata $object $user 1
		unit_writexattr $object $user 1
		unit_accesstime $object $user
		unit_updatetime $object $user 1 
		unit_writeacl $object $user 1
		unit_deletecontent $object $user 1
		unit_deletedata $object $user 1

		if [[ -d $object ]] ;then
			create_object "dir" $object $ZFS_ACL_CUR_USER
		else
			create_object "file" $object $ZFS_ACL_CUR_USER
		fi
	done
}

#
# Testing function to verify the given node is nounlink
#
# $1: The given node, file/dir
#
function test_nounlink
{
	typeset object=$1

	if [[ -z $object ]]; then
		log_fail "Object($object) not defined."
	fi

	$ECHO "Testing nounlink of $object"

	for user in $ZFS_ACL_CUR_USER root $ZFS_ACL_STAFF2; do
		if [[ -d $object ]]; then
			log_must usr_exec chmod \
				A+user:$user:${ace_dir}:allow $object
		else 
			log_must usr_exec chmod \
				A+user:$user:${ace_file}:allow $object
		fi
		log_must set_attribute $object "u"

		unit_writefile $object $user
		unit_writedir $object $user
		unit_appenddata $object $user
		unit_writexattr $object $user
		unit_accesstime $object $user
		unit_updatetime $object $user 
		unit_writeacl $object $user
		unit_deletecontent $object $user 1
		unit_deletedata $object $user 1

		if [[ -d $object ]] ;then
			create_object "dir" $object $ZFS_ACL_CUR_USER
		else
			create_object "file" $object $ZFS_ACL_CUR_USER
		fi
	done
}

#
# Testing function to verify the given node is appendonly
#
# $1: The given node, file/dir
#
function test_appendonly
{	
	typeset object=$1

	if [[ -z $object ]]; then
		log_fail "Object($object) not defined."
	fi

	log_note "Testing appendonly of $object"

	for user in $ZFS_ACL_CUR_USER root $ZFS_ACL_STAFF2; do
		if [[ -d $object ]]; then
			log_must usr_exec chmod \
				A+user:$user:${ace_dir}:allow $object
		else 
			log_must usr_exec chmod \
				A+user:$user:${ace_file}:allow $object
		fi
		log_must set_attribute $object "a"

		unit_writefile $object $user 1
		unit_writedir $object $user
		unit_appenddata $object $user 
		unit_writexattr $object $user
		unit_accesstime $object $user
		unit_updatetime $object $user 
		unit_writeacl $object $user
		unit_deletecontent $object $user
		unit_deletedata $object $user

		if [[ -d $object ]] ;then
			create_object "dir" $object $ZFS_ACL_CUR_USER
		else
			create_object "file" $object $ZFS_ACL_CUR_USER
		fi
	done
}

FILES="file.0 file.1"
DIRS="dir.0 dir.1"
XATTRS="attr.0 attr.1"
FS="$TESTPOOL $TESTPOOL/$TESTFS"

if is_global_zone ; then
	ATTRS="R i u a"
else
	ATTRS="R"
fi

TESTFILE=$TMPDIR/tfile
TESTDIR=tdir
TESTATTR=tattr
TESTACL=user:$ZFS_ACL_OTHER1:write_data:allow
TESTMODE=777
TESTSTR="ZFS test suites"

ace_file="write_data/append_data/write_xattr/write_acl/write_attributes"
ace_dir="add_file/add_subdirectory/${ace_file}"

log_assert "Verify DOS & BSD'ish attributes will provide the " \
	"access limitation as expected."
log_onexit cleanup

$ECHO "$TESTSTR" > $TESTFILE

typeset gobject
typeset gattr
for gattr in $ATTRS ; do
	for fs in $FS ; do
		mtpt=$(get_prop mountpoint $fs)
		$CHMOD 777 $mtpt
		for user in root $ZFS_ACL_STAFF1; do
			log_must set_cur_usr $user		
			for file in $FILES ; do
				gobject=$mtpt/$file
				create_object "file" $gobject $ZFS_ACL_CUR_USER
				test_wrapper $gobject $gattr
				destroy_object $gobject
			done

			for dir in $DIRS ; do
				gobject=$mtpt/$dir
				create_object "dir" $gobject $ZFS_ACL_CUR_USER
				test_wrapper $gobject $gattr
				destroy_object $gobject
			done
		done
	done
done

log_pass "DOS & BSD'ish attributes provide the access limitation as expected."
