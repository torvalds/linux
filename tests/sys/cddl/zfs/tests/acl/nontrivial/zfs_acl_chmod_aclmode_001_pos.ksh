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
# ident	"@(#)zfs_acl_chmod_aclmode_001_pos.ksh	1.3	08/08/15 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_aclmode_001_pos
#
# DESCRIPTION:
#	Verify chmod have correct behaviour to directory and file when
#	filesystem has the different aclmode setting
#	
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Separately chmod basedir with different aclmode options,
#	 	combine with the variable setting of aclmode:
#		"discard", "groupmask", or "passthrough".
#	4. Verify each directories and files have the correct access control
#	   capability.
#	
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-03-02)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

test_requires ZFS_ACL

function cleanup
{
	# Cleanup tarfile & basedir.

	(( ${#cwd} != 0 )) && cd $cwd

	if [[ -f $TARFILE ]]; then
		log_must $RM -f $TARFILE
	fi

	if [[ -d $basedir ]]; then
		log_must $RM -rf $basedir
	fi
}

log_assert "Verify chmod have correct behaviour to directory and file when " \
	"filesystem has the different aclmode setting."
log_onexit cleanup

# Define aclmode flag
set -A aclmode_flag discard groupmask passthrough

set -A ace_prefix "user:$ZFS_ACL_OTHER1" \
		"user:$ZFS_ACL_OTHER2" \
		"group:$ZFS_ACL_STAFF_GROUP" \
		"group:$ZFS_ACL_OTHER_GROUP"

set -A argv  "000" "444" "644" "777" "755" "231" "562" "413"

set -A ace_file_preset "read_data" \
		"write_data" \
		"append_data" \
		"execute" \
		"read_data/write_data" \
		"read_data/write_data/append_data" \
		"write_data/append_data" \
		"read_data/execute" \
		"write_data/append_data/execute" \
		"read_data/write_data/append_data/execute"

# Defile the based directory and file
basedir=$TESTDIR/basedir;  ofile=$basedir/ofile; odir=$basedir/odir
nfile=$basedir/nfile; ndir=$basedir/ndir

TARFILE=$TESTDIR/tarfile

# Verify all the node have expected correct access control
allnodes="$nfile $ndir"

#
# According to the original bits, the input ACE access and ACE type, return the
# expect bits after 'chmod A0{+|=}'.
#
# $1 isdir indicate if the target is a directory
# $1 bits which was make up of three bit 'rwx'
# $2 bits_limit which was make up of three bit 'rwx'
# $3 ACE access which is read_data, write_data or execute
# $4 ACE type which is allow or deny
#
function cal_bits #isdir bits bits_limit acl_access ctrl
{
	typeset -i isdir=$1
	typeset -i bits=$2
	typeset -i bits_limit=$3
	typeset acl_access=$4
	typeset -i ctrl=${5:-0}
	typeset flagr=0; flagw=0; flagx=0
	typeset tmpstr

	if (( ctrl == 0 )); then 
		if (( (( bits & 4 )) == 0 )); then
			flagr=1
		fi
		if (( (( bits & 2 )) == 0 )); then
			flagw=1
		fi
		if (( (( bits & 1 )) == 0 )); then
			flagx=1
		fi
	else
		#
		# Tricky here: 
		# (1) flagr is always set to be 1,
		# (2) flagw & flagx is set to be 0 only while
		#	bits_limit has lower permissions than bits
		#

		flagr=1
		flagw=1
		flagx=1

		if (( (( bits & 2 )) != 0 )) && \
			(( (( bits_limit & 2 )) == 0 )) ; then
			flagw=0
		fi
		if (( (( bits & 1 )) != 0 )) && \
			(( (( bits_limit & 1 )) == 0 )) ; then
			flagx=0
		fi
	fi

	if (( flagr != 0 )); then
		if [[ $acl_access == *"read_data"* ]]; then
			if (( isdir == 0 )) ; then
				tmpstr=${tmpstr}/read_data
			else
				tmpstr=${tmpstr}/list_directory/read_data
			fi
		fi
	fi

	if (( flagw != 0 )); then
		if [[ $acl_access == *"write_data"* ]]; then
			if (( isdir == 0 )); then
				tmpstr=${tmpstr}/write_data
			else
				tmpstr=${tmpstr}/add_file/write_data
			fi
		fi

		if [[ $acl_access == *"append_data"* ]]; then
			if (( isdir == 0 )); then
				tmpstr=${tmpstr}/append_data
			else
				tmpstr=${tmpstr}/add_subdirectory/append_data
			fi
		fi
	fi
	if (( flagx != 0 )); then
		[[ $acl_access == *"execute"* ]] && \
			tmpstr=${tmpstr}/execute
	fi

	tmpstr=${tmpstr#/}

	$ECHO "$tmpstr"
}

#
# To translate an ace if the node is dir
#
# $1 isdir indicate if the target is a directory
# $2 acl to be translated
#
function translate_acl #isdir acl
{
	typeset -i isdir=$1
	typeset acl=$2
	typeset who prefix acltemp action
	
	if (( isdir != 0 )); then
		who=${acl%%:*}
		prefix=$who
		acltemp=${acl#*:}
		acltemp=${acltemp%%:*}
		prefix=$prefix:$acltemp
		action=${acl##*:}

		acl=$prefix:$(cal_bits $isdir 7 7 $acl 1):$action
	fi
	$ECHO "$acl"
}

#
# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
#
function verify_aclmode #<aclmode> <node> <newmode>
{
	# Define the nodes which will be affected by inherit.
	typeset aclmode=$1
	typeset node=$2
	typeset newmode=$3

	# count: the ACE item to fetch
	# pass: to mark if the current ACE should apply to the target
	# passcnt: counter, if it achieves to maxnumber, 
	#	then no additional ACE should apply.
	# step: indicate if the ACE be split during aclmode.

	typeset -i count=0 pass=0 passcnt=0 step=0
	typeset -i bits=0 obits=0 bits_owner=0 isdir=0

	if [[ -d $node ]]; then
		(( isdir = 1 ))
	fi 

	(( i = maxnumber - 1 ))
	count=0
	passcnt=0
	while (( i >= 0 )); do
		pass=0
		step=0
		expect1=${acls[$i]}
		expect2=""

		#
		# aclmode=passthrough,
		# no changes will be made to the ACL other than 
		# generating the necessary ACL entries to represent
		# the new mode of the file or directory. 
		#
		# aclmode=discard,
		# delete all ACL entries that don't represent 
		# the mode of the file.
		#
		# aclmode=groupmask,
		# reduce user or group permissions.  The permissions are 
		# reduced, such that they are no greater than the group 
		# permission bits, unless it is a user entry that has the 
		# same UID as the owner of the file or directory.
		# Then, the ACL permissions are reduced so that they are 
		# no greater than owner permission bits.
		#

		case $aclmode in
			passthrough)
				expect1=$(translate_acl $isdir $expect1)
				;;
			groupmask)
				if [[ $expect1 == *":allow" ]]; then
					expect2=$expect1
					who=${expect1%%:*}
					prefix=$who
					acltemp=""
					reduce=0

					# To determine the mask bits
					# according to the entry type.

					case $who in
						owner@)
							pos=1
							;;
						group@)
							pos=2
							;;
						everyone@)
							pos=3
							;;
						user)
							acltemp=${expect1#*:}
							acltemp=${acltemp%%:*}
							owner=$(get_owner $node)
							group=$(get_group $node)
							if [[ $acltemp == $owner ]]; then
								pos=1
							else
								pos=2
							fi							
							prefix=$prefix:$acltemp
							;;
						group)
							acltemp=${expect1#*:}
							acltemp=${acltemp%%:*}
							pos=2
							prefix=$prefix:$acltemp
							reduce=1
							;;
					esac
						
					obits=$(get_substr $newmode $pos 1)
					(( bits = obits ))
		#
		# permission should no greater than the group permission bits
		#
					if (( reduce != 0 )); then
						(( bits &= $(get_substr $newmode 2 1) ))

		# The ACL permissions are reduced so that they are
                # no greater than owner permission bits.

						(( bits_owner = $(get_substr $newmode 1 1) ))
						(( bits &= bits_owner ))
					fi

					if (( bits < obits )) && [[ -n $acltemp ]]; then
						expect2=$prefix:$(cal_bits $isdir $obits $bits_owner $expect2 1):allow
					else
						expect2=$prefix:$(cal_bits $isdir $obits $obits $expect2 1):allow
		
					fi

					priv=$(cal_bits $isdir $obits $bits_owner $expect2 0)
					expect1=$prefix:$priv:deny
					step=1
				else
					expect1=$(translate_acl $isdir $expect1)
				fi
				;;
			discard)
				passcnt=maxnumber
				break
				;;
		esac

		if (( pass == 0 )) ; then
			# Get the first ACE to do comparison

			aclcur=$(get_ACE $node $count)
			aclcur=${aclcur#$count:}
			if [[ -n $expect1 && $expect1 != $aclcur ]]; then
				$LS -vd $node
				log_fail "$i #$count " \
					"ACE: $aclcur, expect to be " \
					"$expect1"
			fi

			# Get the second ACE (if should have) to do comparison

			if (( step > 0 )); then
				(( count = count + step ))

				aclcur=$(get_ACE $node $count)
				aclcur=${aclcur#$count:}
				if [[ -n $expect2 && \
					$expect2 != $aclcur ]]; then

					$LS -vd $node
					log_fail "$i #$count " \
						"ACE: $aclcur, expect to be " \
						"$expect2"
				fi
			fi
			(( count = count + 1 ))
		fi
		(( i = i - 1 ))
	done

	#
	# If there's no any ACE be checked, it should be identify as
	# an normal file/dir, verify it.
	#
 
	if (( passcnt == maxnumber )); then
		if [[ -d $node ]]; then
			compare_acls $node $odir
		elif [[	-f $node ]]; then
			compare_acls $node $ofile
		fi

		if [[ $? -ne 0 ]]; then
			$LS -vd $node
			log_fail "Unexpect acl: $node, $aclmode ($newmode)"
		fi
	fi
}



typeset -i maxnumber=0
typeset acl
typeset target

cwd=$PWD
cd $TESTDIR

for mode in "${aclmode_flag[@]}"; do

	#
	# Set different value of aclmode
	#

	log_must $ZFS set aclmode=$mode $TESTPOOL/$TESTFS

	for user in root $ZFS_ACL_STAFF1; do
		log_must set_cur_usr $user

		log_must usr_exec $MKDIR $basedir

		log_must usr_exec $MKDIR $odir
		log_must usr_exec $TOUCH $ofile 
		log_must usr_exec $MKDIR $ndir
		log_must usr_exec $TOUCH $nfile 

		for obj in $allnodes ; do
			maxnumber=0
			for preset in "${ace_file_preset[@]}"; do
				for prefix in "${ace_prefix[@]}"; do
					acl=$prefix:$preset

					case $(( maxnumber % 2 )) in
						0)
							acl=$acl:deny
							;;
						1)
							acl=$acl:allow
							;;
					esac

				#
				# Place on the target should succeed.
				#
					log_must usr_exec $CHMOD A+$acl $obj
					acls[$maxnumber]=$acl

					(( maxnumber = maxnumber + 1 ))
				done
			done

			# Archive the file and directory
			log_must $TAR cpf@ $TARFILE basedir

			if [[ -d $obj ]]; then
				target=$odir
			elif [[ -f $obj ]]; then
				target=$ofile
               		fi

			for newmode in "${argv[@]}" ; do
				log_must usr_exec $CHMOD $newmode $obj
				log_must usr_exec $CHMOD $newmode $target
				verify_aclmode $mode $obj $newmode

			 	# Restore the tar archive
				log_must $TAR xpf@ $TARFILE
			done
		done

		log_must usr_exec $RM -rf $basedir $TARFILE
	done
done

log_pass "Verify chmod behaviour co-op with aclmode setting passed."
