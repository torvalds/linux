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
# ident	"@(#)mountpoint_003_pos.ksh	1.5	09/01/13 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: mountpoint_003_pos
#
# DESCRIPTION:
#	Verify FSType-specific option works well with legacy mount.
#
# STRATEGY:
#	1. Set up FSType-specific options and expected keywords array.
#	2. Create a test ZFS file system and set mountpoint=legacy.
#	3. Mount ZFS test filesystem with specific options.
#	4. Verify the filesystem was mounted with specific option.
#	5. Loop check all the options.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-01-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	ismounted $tmpmnt && log_must $UMOUNT $tmpmnt 
	[[ -d $tmpmnt ]] && log_must $RM -rf $tmpmnt
	[[ -n $oldmpt ]] && log_must $ZFS set mountpoint=$oldmpt $testfs
	! ismounted $oldmpt && log_must $ZFS mount $testfs
}

log_assert "With legacy mount, FSType-specific option works well."
log_onexit cleanup

#
#  /mnt on pool/fs read/write/setuid/devices/noexec/xattr/atime/dev=2d9009e
#
#	FSType-				FSType-
#	specific	Keyword		specific	Keyword
#	option				option
#
set -A args \
	"devices"	"/devices/"	"nodevices"	"/nodevices/"	\
	"exec"		"/exec/"	"noexec"	"/noexec/"	\
	"nbmand"	"/nbmand/"	"nonbmand"	"/nonbmand/"	\
	"ro"		"read only"	"rw"		"read/write"	\
	"setuid"	"/setuid/"	"nosetuid"	"/nosetuid/"	\
	"xattr"		"/xattr/"	"noxattr"	"/noxattr/"	\
	"atime"		"/atime/"	"noatime"	"/noatime/"

tmpmnt=/tmpmnt.${TESTCASE_ID}
[[ -d $tmpmnt ]] && $RM -rf $tmpmnt
testfs=$TESTPOOL/$TESTFS 
log_must $MKDIR $tmpmnt
oldmpt=$(get_prop mountpoint $testfs)
log_must $ZFS set mountpoint=legacy $testfs

typeset i=0
while ((i < ${#args[@]})); do
	log_must $MOUNT -t zfs -o ${args[$i]} $testfs $tmpmnt
	msg=$($MOUNT | $GREP "^$tmpmnt ") 

	# In LZ, a user with all zone privileges can never with "devices"
	if ! is_global_zone && [[ ${args[$i]} == devices ]] ; then
		args[((i+1))]="/nodevices/"
	fi
		
	$ECHO $msg | $GREP "${args[((i+1))]}" > /dev/null 2>&1
	if (($? != 0)) ; then
		log_fail "Expected option: ${args[((i+1))]} \n" \
			 "Real option: $msg"
	fi
	
	log_must $UMOUNT $tmpmnt
	((i += 2))
done

log_pass "With legacy mount, FSType-specific option works well passed."
