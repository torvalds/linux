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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"@(#)zpool_create_017_neg.ksh	1.1	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_017_neg
#
#
# DESCRIPTION:
# 'zpool create' will fail with mountpoint exists and is not empty.
#
#
# STRATEGY:
# 1. Prepare the mountpoint put some stuff into it.
# 2. Verify 'zpool create' over that mountpoint fails.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-02)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	if [[ -d $TESTDIR ]]; then
		log_must $RM -rf $TESTDIR
	fi
}

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

typeset pool_dev=${disk}p1

log_assert "'zpool create' should fail with mountpoint exists and not empty."
log_onexit cleanup

if [[ ! -d $TESTDIR ]]; then
	log_must $MKDIR -p $TESTDIR
fi

typeset -i i=0

while (( i < 2 )); do
	log_must $RM -rf $TESTDIR/*
	if (( i == 0 )); then
		log_must $MKDIR $TESTDIR/testdir
	else
		log_must $TOUCH $TESTDIR/testfile
	fi

	log_mustnot $ZPOOL create -m $TESTDIR -f $TESTPOOL $pool_dev
	log_mustnot poolexists $TESTPOOL

	(( i = i + 1 ))
done

log_pass "'zpool create' fail as expected with mountpoint exists and not empty."
