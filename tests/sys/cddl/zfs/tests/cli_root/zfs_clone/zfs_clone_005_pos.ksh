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
# ident	"@(#)zfs_clone_005_pos.ksh	1.1	09/01/13 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/properties.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_clone_005_pos
#
# DESCRIPTION:
# 'zfs clone -o property=value -V size volume' can successfully create a ZFS
# clone volume with correct property set. 
#
# STRATEGY:
# 1. Create a ZFS clone volume in the storage pool with -o option
# 2. Verify the volume created successfully
# 3. Verify the property is correctly set
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-12-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if snapexists $SNAPFS1 ; then
		log_must $ZFS destroy -Rf $SNAPFS1
	fi
}

if ! $(check_opt_support "clone" "-o") ; then
	log_unsupported "'zfs clone -o' unsupported."
fi

log_onexit cleanup

log_assert "'zfs clone -o property=value -V size volume' can successfully" \
	   "create a ZFS clone volume with correct property set."

log_must $ZFS snapshot $SNAPFS1
typeset -i i=0
while (( $i < ${#RW_VOL_CLONE_PROP[*]} )); do 
	if [[ $WRAPPER == *"crypto"* ]] && \
		[[ ${RW_VOL_CLONE_PROP[$i]} == *"checksum"* ]]; then
		(( i = i + 1 ))
		continue
	fi

	log_must $ZFS clone -o ${RW_VOL_CLONE_PROP[$i]} $SNAPFS1 $TESTPOOL/$TESTCLONE
	datasetexists $TESTPOOL/$TESTCLONE || \
		log_fail "zfs clone $TESTPOOL/$TESTCLONE fail."
	propertycheck $TESTPOOL/$TESTCLONE ${RW_VOL_CLONE_PROP[i]} || \
		log_fail "${RW_VOL_CLONE_PROP[i]} is failed to set."
	log_must $ZFS destroy -f $TESTPOOL/$TESTCLONE

	(( i = i + 1 ))
done

log_pass "'zfs clone -o property=value volume' can successfully" \
	"create a ZFS clone volume with correct property set."
