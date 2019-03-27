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
# ident	"@(#)zfs_create_005_pos.ksh	1.4	09/01/13 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/properties.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_005_pos
#
# DESCRIPTION:
# 'zfs create -o property=value filesystem' can successfully create a ZFS
# filesystem with multiple properties set. 
#
# STRATEGY:
# 1. Create a ZFS filesystem in the storage pool with multiple -o options
# 2. Verify the filesystem created successfully
# 3. Verify the properties are correctly set
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

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS1
}

log_onexit cleanup


log_assert "'zfs create -o property=value filesystem' can successfully create \
	   a ZFS filesystem with multiple properties set."

typeset -i i=0
typeset opts=""

while (( $i < ${#RW_FS_PROP[*]} )); do 
        if [[ $WRAPPER != *"crypto"* ]] || \
                [[ ${RW_FS_PROP[$i]} != *"checksum"* ]]; then
		opts="$opts -o ${RW_FS_PROP[$i]}"
	fi
	(( i = i + 1 ))
done

log_must $ZFS create $opts $TESTPOOL/$TESTFS1
datasetexists $TESTPOOL/$TESTFS1 || \
	log_fail "zfs create $TESTPOOL/$TESTFS1 fail."

i=0
while (( $i < ${#RW_FS_PROP[*]} )); do 
        if [[ $WRAPPER != *"crypto"* ]] || \
                [[ ${RW_FS_PROP[$i]} != *"checksum"* ]]; then
		propertycheck $TESTPOOL/$TESTFS1 ${RW_FS_PROP[i]} || \
			log_fail "${RW_FS_PROP[i]} is failed to set."
	fi
	(( i = i + 1 ))
done

log_pass "'zfs create -o property=value filesystem' can successfully create \
         a ZFS filesystem with multiple properties set."
