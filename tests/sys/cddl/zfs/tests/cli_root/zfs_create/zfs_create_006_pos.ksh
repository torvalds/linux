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
# ident	"@(#)zfs_create_006_pos.ksh	1.4	09/01/13 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/properties.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_006_pos
#
# DESCRIPTION:
# 'zfs create -o property=value -V size volume' can successfully create a ZFS
# volume with correct property set. 
#
# STRATEGY:
# 1. Create a ZFS volume in the storage pool with -o option
# 2. Verify the volume created successfully
# 3. Verify the property is correctly set
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/$TESTVOL1 && \
		log_must $ZFS destroy -f $TESTPOOL/$TESTVOL1
}

log_onexit cleanup


log_assert "'zfs create -o property=value -V size volume' can successfully \
	   create a ZFS volume with correct property set."

typeset -i i=0
while (( $i < ${#RW_VOL_PROP[*]} )); do 
	if [[ $WRAPPER == *"crypto"* ]] && \
		[[ ${RW_VOL_PROP[$i]} == *"checksum"* ]]; then
		(( i = i + 1 ))
		continue
	fi

	log_must $ZFS create -o ${RW_VOL_PROP[$i]} -V $VOLSIZE \
		$TESTPOOL/$TESTVOL1
	datasetexists $TESTPOOL/$TESTVOL1 || \
		log_fail "zfs create -V size $TESTPOOL/$TESTVOL1 fail."
	propertycheck $TESTPOOL/$TESTVOL1 ${RW_VOL_PROP[i]} || \
		log_fail "${RW_VOL_PROP[i]} is failed to set."
	log_must $ZFS destroy -f $TESTPOOL/$TESTVOL1

	log_must $ZFS create -s -o ${RW_VOL_PROP[$i]} -V $VOLSIZE \
		$TESTPOOL/$TESTVOL1
	datasetexists $TESTPOOL/$TESTVOL1 || \
		log_fail "zfs create -s -V $TESTPOOL/$TESTVOL1 fail."
	propertycheck $TESTPOOL/$TESTVOL1 ${RW_VOL_PROP[i]} || \
		log_fail "${RW_VOL_PROP[i]} is failed to set."
	log_must $ZFS destroy -f $TESTPOOL/$TESTVOL1

	(( i = i + 1 ))
done

log_pass "'zfs create -o property=value -V size volume' can successfully \
	   create a ZFS volume with correct property set."
