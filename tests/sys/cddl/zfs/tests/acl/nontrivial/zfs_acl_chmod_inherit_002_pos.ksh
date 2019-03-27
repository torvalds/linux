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
# ident	"@(#)zfs_acl_chmod_inherit_002_pos.ksh	1.3	08/02/27 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib
. $STF_SUITE/tests/acl/cifs/cifs.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_chmod_inherit_002_pos
#
# DESCRIPTION:
#	Verify chmod have correct behaviour to directory and file when
#	filesystem has the different aclinherit setting
#	
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Separately chmod basedir with different inherite options,
#	 	combine with the variable setting of aclinherit:
#		"discard", "noallow", "secure" or "passthrough".
#	4. Then create nested directories and files like the following.
#	
#                     ofile    
#                     odir     	
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
# CODING_STATUS: COMPLETED (2006-03-02)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset dir

	# Cleanup basedir, compared file and dir.

	if [[ -f $ofile ]]; then
		log_must $RM -f $ofile
	fi

	for dir in $odir $basedir ; do
		if [[ -d $dir ]]; then
			log_must $RM -rf $dir
		fi
	done		
}

log_assert "Verify chmod have correct behaviour to directory and file when " \
	"filesystem has the different aclinherit setting."
log_onexit cleanup

# Define inherit flag
set -A aclinherit_flag discard noallow secure passthrough
set -A object_flag file_inherit dir_inherit file_inherit/dir_inherit
set -A strategy_flag "" inherit_only no_propagate inherit_only/no_propagate

typeset ace_prefix1="user:$ZFS_ACL_OTHER1" 
typeset ace_prefix2="user:$ZFS_ACL_OTHER2" 
typeset ace_discard ace_noallow ace_secure ace_passthrough
typeset ace_secure_new

# Defile the based directory and file
basedir=$TESTDIR/basedir;  ofile=$TESTDIR/ofile; odir=$TESTDIR/odir

test_requires ZFS_ACL

# Define the files and directories will be created after chmod
ndir1=$basedir/ndir1; ndir2=$ndir1/ndir2; ndir3=$ndir2/ndir3
nfile1=$basedir/nfile1; nfile2=$ndir1/nfile2; nfile3=$ndir2/nfile3

# Verify all the node have expected correct access control
allnodes="$ndir1 $ndir2 $ndir3 $nfile1 $nfile2 $nfile3"

typeset cifs=""
if cifs_supported ; then
	cifs="true"
fi

#
# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
#
function verify_inherit #<aclinherit> <object> [strategy]
{
	# Define the nodes which will be affected by inherit.
	typeset inherit_nodes
	typeset inherit=$1
	typeset obj=$2
	typeset str=$3

	# count: the ACE item to fetch
	# pass: to mark if the current ACE should apply to the target
	# maxnumber: predefine as 4
	# passcnt: counter, if it achieves to maxnumber, 
	#	then no additional ACE should apply.
	# isinherit: indicate if the current target is in the inherit list.
	# step: indicate if the ACE be split during inherit.

	typeset -i count=0 pass=0 passcnt=0 isinherit=0 maxnumber=4 step=0

	log_must usr_exec $MKDIR -p $ndir3
	log_must usr_exec $TOUCH $nfile1 $nfile2 $nfile3

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
		step=0
		if [[ " $inherit_nodes " == *" $node "* ]]; then
			isinherit=1
			if [[ -d $node ]] ; then
				step=1
			fi
		else
			isinherit=0
		fi

		i=0
		count=0
		passcnt=0
		while (( i < maxnumber )); do
			pass=0
			eval expect1=\$acl$i
			expect2=$expect1

		#
		# aclinherit=passthrough,
		# inherit all inheritable ACL entries without any 
		# modifications made to the ACL entries when they
		# are inherited.
		#
		# aclinherit=secure,
		# any inheritable ACL entries will remove
		# write_acl and write_owner permissions when the ACL entry is
		# inherited.
		#
		# aclinherit=noallow,
		# only inherit inheritable ACE that specify "deny" permissions
		#
		# aclinherit=discard
		# will not inherit any ACL entries
		#

			case $inherit in
				passthrough)
					;;
				secure)
					if [[ $expect1 == *":allow" ]] ; then
						eval expect2=\$acls$i
					fi
					;;
				noallow)
					if [[ $expect1 == *":allow" ]] ; then
						pass=1
						(( passcnt = passcnt + 1 ))
					fi
					;;	
				discard)
					passcnt=maxnumber
					break
					;;
			esac

			if (( pass == 0 )) ; then
				acltemp=${expect2%:*}
				acltemp=${acltemp%:*}
				aclaction=${expect2##*:}

				if [[ -n $cifs ]]; then
					expect2=${acltemp}:inherited:${aclaction}
				else
					expect2=${acltemp}:${aclaction}
				fi

				acltemp=${expect1%:*}

				if [[ -d $node ]]; then
					if [[ $expect1 == *"no_propagate"* ]] ; then

						#
						# if no_propagate is set,
						# then clear all inherit flags,
						# only one ACE should left.
						#

						step=0
						expect1=""
							
					elif [[ $expect1 != *"inherit_only"* ]]; then

						#
						# directory should append 
						# "inherit_only" if not have
						#

						if [[ -n $cifs ]]; then

							expect1=${acltemp}/inherit_only/inherited:${aclaction}
						else
							expect1=${acltemp}/inherit_only:${aclaction}
						fi
					elif [[ -n $cifs ]]; then
						expect1=${acltemp}/inherited:${aclaction}
					fi

					#
					# cleanup the first ACE if the directory 
					# not in inherit list
					#

					if (( isinherit == 0 )); then
						expect1=""
					fi
				elif [[ -f $node ]] ; then
					expect1=""
				fi

				# Get the first ACE to do comparison

				aclcur=$(get_ACE $node $count)
				aclcur=${aclcur#$count:}
				if [[ -n $expect1 && $expect1 != $aclcur ]]; then
					$LS -vd $basedir
					$LS -vd $node
					log_fail "$inherit $i #$count " \
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

						$LS -vd $basedir
						$LS -vd $node
						log_fail "$inherit $i #$count " \
							"ACE: $aclcur, expect to be " \
							"$expect2"
					fi
				fi
				(( count = count + 1 ))
			fi
			(( i = i + 1 ))
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
				$LS -vd $basedir
				$LS -vd $node
				log_fail "Unexpect acl: $node, $inherit ($str)"
			fi
		fi
	done
}

typeset -i i=0
typeset acl0 acl1 acl2 acl3
typeset acls0 acls1 acls2 acls3

#
# Set aclmode=passthrough to make sure
# the acl will not change during chmod.
# A general testing should verify the combination of 
# aclmode/aclinherit works well,
# here we just simple test them separately.
#

log_must $ZFS set aclmode=passthrough $TESTPOOL/$TESTFS

for inherit in "${aclinherit_flag[@]}"; do

	#
	# Set different value of aclinherit
	#

	log_must $ZFS set aclinherit=$inherit $TESTPOOL/$TESTFS

	for user in root $ZFS_ACL_STAFF1; do
		log_must set_cur_usr $user

		for obj in "${object_flag[@]}"; do
			for str in "${strategy_flag[@]}"; do
				typeset inh_opt=$obj
				(( ${#str} != 0 )) && inh_opt=$inh_opt/$str

				#
				# Prepare 4 ACES, which should include :
				# deny -> to verify "noallow"
				# write_acl/write_owner -> to verify "secure"
				#

				acl0="$ace_prefix1:read_xattr/write_acl/write_owner:$inh_opt:deny"
				acl1="$ace_prefix2:read_xattr/write_acl/write_owner:$inh_opt:allow"
				acl2="$ace_prefix1:read_xattr:$inh_opt:deny"
				acl3="$ace_prefix2:read_xattr:$inh_opt:allow"

				#
				# The ACE filtered by write_acl/write_owner
				#

				acls0="$ace_prefix1:read_xattr:$inh_opt:deny"
				acls1="$ace_prefix2:read_xattr:$inh_opt:allow"
				acls2=$acl2
				acls3=$acl3


				#
				# Create basedir and tmp dir/file
				# for comparison.
				#
				
				log_note "$user: $CHMOD $acl $basedir"
				log_must usr_exec $MKDIR $basedir
				log_must usr_exec $MKDIR $odir
				log_must usr_exec $TOUCH $ofile 

				i=3
				while (( i >= 0 )); do
					eval acl=\$acl$i 

				#
				# Place on a directory should succeed.
				#
					log_must usr_exec $CHMOD A+$acl $basedir

					(( i = i - 1 ))
				done
			
				verify_inherit $inherit $obj $str
			
				log_must usr_exec $RM -rf $ofile $odir $basedir
			done
		done
	done
done

log_pass "Verify chmod inherit behaviour co-op with aclinherit setting passed."
