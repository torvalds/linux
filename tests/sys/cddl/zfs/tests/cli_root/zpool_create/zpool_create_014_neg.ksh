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
#ident	"@(#)zpool_create_014_neg.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_014_neg
#
#
# DESCRIPTION:
# 'zpool create' will fail with ordinary file in swap
#
# STRATEGY:
# 1. Create a regular file on top of UFS-zvol filesystem
# 2. Try to create a new pool with regular file in swap
# 3. Verify the creation is failed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-04-17)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if datasetexists $vol_name; then
		$SWAP -l | $GREP $TMP_FILE > /dev/null 2>&1
		if [[ $? -eq 0 ]]; then
			log_must $SWAP -d $TMP_FILE
		fi
		$RM -f $TMP_FILE
		log_must $UMOUNT $mntp
		$ZFS destroy $vol_name
	fi
	
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
}

log_assert "'zpool create' should fail with regular file in swap."
log_onexit cleanup

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

typeset pool_dev=${disk}p1
typeset vol_name=$TESTPOOL/$TESTVOL
typeset mntp=$TMPDIR
typeset TMP_FILE=$mntp/tmpfile.${TESTCASE_ID}

create_pool $TESTPOOL $pool_dev
log_must $ZFS create -V 100m $vol_name
log_must $ECHO "y" | $NEWFS /dev/zvol/$vol_name > /dev/null 2>&1
log_must $MOUNT /dev/zvol/$vol_name $mntp

log_must $MKFILE 50m $TMP_FILE
log_must $SWAP -a $TMP_FILE

for opt in "-n" "" "-f"; do
	log_mustnot $ZPOOL create $opt $TESTPOOL $TMP_FILE
done

log_pass "'zpool create' passed as expected with inapplicable scenario."
