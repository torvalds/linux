#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)grow_pool_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: grow_pool_001_pos
#
# DESCRIPTION:
# A ZFS file system is limited by the amount of disk space
# available to the pool. Growing the pool by adding a disk
# increases the amount of space.
#
# STRATEGY:
# 1) Fill a ZFS filesystem until ENOSPC by creating a large file
# 2) Grow the pool by adding a disk
# 3) Verify that more data can now be written to the file system
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "A zpool may be increased in capacity by adding a disk"

log_must $ZFS set compression=off $TESTPOOL/$TESTFS
$FILE_WRITE -o create -f $TESTDIR/$TESTFILE1 \
	-b $BLOCK_SIZE -c $WRITE_COUNT -d 0
typeset -i zret=$?
readonly ENOSPC=28
if [[ $zret -ne $ENOSPC ]]; then
	log_fail "file_write completed w/o ENOSPC, aborting!!!"
fi

if [[ ! -s $TESTDIR/$TESTFILE1 ]]; then
	log_fail "$TESTDIR/$TESTFILE1 was not created"
fi

if [[ -n $DISK ]]; then
	log_must $ZPOOL add $TESTPOOL ${DISK}p2
else
	log_must $ZPOOL add $TESTPOOL $DISK1
fi

log_must $FILE_WRITE -o append -f $TESTDIR/$TESTFILE1 \
	-b $BLOCK_SIZE -c $SMALL_WRITE_COUNT -d 0

log_must $ZFS inherit compression $TESTPOOL/$TESTFS
log_pass "TESTPOOL successfully grown"
