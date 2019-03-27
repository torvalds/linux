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
# ident	"@(#)zpool_create_021_pos.ksh	1.2	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_create/zfs_create_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_021_pos
#
# DESCRIPTION:
# 'zpool create -O property=value pool' can successfully create a pool 
# with correct filesystem property set. 
#
# STRATEGY:
# 1. Create a storage pool with -O option
# 2. Verify the pool created successfully
# 3. Verify the filesystem property is correctly set
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-27)
#
# __stc_assertion_end
#
################################################################################

if ! $(check_zpool_opt_support "create" "-O") ; then
	log_unsupported "-O option is not supported yet."
fi

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

log_assert "'zpool create -O property=value pool' can successfully create a pool \
		with correct filesystem property set."

set -A RW_FS_PROP "quota=512M" \
		  "reservation=512M" \
		  "recordsize=64K" \
		  "mountpoint=/tmp/mnt${TESTCASE_ID}" \
		  "checksum=fletcher2" \
		  "compression=lzjb" \
		  "atime=off" \
		  "devices=off" \
		  "exec=off" \
		  "setuid=off" \
		  "readonly=on" \
		  "snapdir=visible" \
		  "aclmode=discard" \
		  "aclinherit=discard" \
		  "canmount=off" \
		  "sharenfs=on"

typeset -i i=0
while (( $i < ${#RW_FS_PROP[*]} )); do 
	log_must $ZPOOL create -O ${RW_FS_PROP[$i]} -f $TESTPOOL $DISKS
	datasetexists $TESTPOOL || \
		log_fail "zpool create $TESTPOOL fail."
	propertycheck $TESTPOOL ${RW_FS_PROP[i]} || \
		log_fail "${RW_FS_PROP[i]} is failed to set."
	destroy_pool $TESTPOOL
	(( i = i + 1 ))
done

log_pass "'zpool create -O property=value pool' can successfully create a pool \
		with correct filesystem property set."

