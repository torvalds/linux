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
# ident	"@(#)zpool_create_010_neg.ksh	1.3	07/02/06 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_010_neg
#
# DESCRIPTION:
# 'zpool create' should return an error with VDEVsof size  <64mb
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute 'zpool create'
# 3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-30)
#
# __stc_assertion_end
#
################################################################################

log_assert "'zpool create' should return an error with VDEVs <64mb"

verify_runnable "global"

function cleanup
{
        poolexists $TOOSMALL && destroy_pool $TOOSMALL
        poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

        poolexists $TESTPOOL && destroy_pool $TESTPOOL

	[[ -d $TESTDIR ]] && $RM -rf $TESTDIR
}
log_onexit cleanup

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

create_pool $TESTPOOL $disk
log_must $ZFS create $TESTPOOL/$TESTFS
log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

VDEV_SIZE=63m
log_must create_vdevs $TESTDIR/file1 $TESTDIR/file2
unset VDEV_SIZE

set -A args \
	"$TOOSMALL $TESTDIR/file1" "$TESTPOOL1 $TESTDIR/file1 $TESTDIR/file2" \
        "$TOOSMALL mirror $TESTDIR/file1 $TESTDIR/file2" \
	"$TOOSMALL raidz $TESTDIR/file1 $TESTDIR/file2"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot $ZPOOL create ${args[i]}
	((i = i + 1))
done

log_pass "'zpool create' with badly formed parameters failed as expected."
