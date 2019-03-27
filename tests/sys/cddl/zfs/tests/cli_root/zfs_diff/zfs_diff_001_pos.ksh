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
# Copyright 2017 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

verify_runnable "both"

log_assert "'zfs diff' output for typical operations"

# First create a bunch of files and directories

#log_must ${CD} $TESTDIR
log_must ${MKDIR} ${TESTDIR}/dirs
log_must ${MKDIR} ${TESTDIR}/dirs/leavealone
log_must ${MKDIR} ${TESTDIR}/dirs/modify
log_must ${MKDIR} ${TESTDIR}/dirs/rename
log_must ${MKDIR} ${TESTDIR}/dirs/delete
log_must ${MKDIR} ${TESTDIR}/files
log_must ${TOUCH} ${TESTDIR}/files/leavealone
log_must ${TOUCH} ${TESTDIR}/files/modify
log_must ${TOUCH} ${TESTDIR}/files/rename
log_must ${TOUCH} ${TESTDIR}/files/delete
log_must ${MKDIR} ${TESTDIR}/files/srcdir
log_must ${MKDIR} ${TESTDIR}/files/dstdir
log_must ${TOUCH} ${TESTDIR}/files/srcdir/move

log_must $ZFS snapshot $TESTPOOL/$TESTFS@1

# Now modify them in different ways
log_must ${TOUCH} ${TESTDIR}/dirs/modify
log_must ${MV} ${TESTDIR}/dirs/rename ${TESTDIR}/dirs/rename.new
log_must ${RMDIR} ${TESTDIR}/dirs/delete
log_must ${MKDIR} ${TESTDIR}/dirs/create
log_must ${DATE} >> ${TESTDIR}/files/modify
log_must ${MV} ${TESTDIR}/files/rename ${TESTDIR}/files/rename.new
log_must ${RM} ${TESTDIR}/files/delete
log_must ${MV} ${TESTDIR}/files/srcdir/move ${TESTDIR}/files/dstdir/move
log_must ${TOUCH} ${TESTDIR}/files/create

log_must $ZFS snapshot $TESTPOOL/$TESTFS@2

# "zfs diff"'s output order is unspecified, so we must sort it.  The golden
# file is already sorted.
LC_ALL=C $ZFS diff $TESTPOOL/$TESTFS@1 $TESTPOOL/$TESTFS@2 | ${SORT} > $TESTDIR/zfs_diff_output.txt
if [ $? -ne 0 ]; then
	log_fail "zfs diff failed"
fi

# Finally, compare output to the golden output
log_must diff $STF_SUITE/tests/cli_root/zfs_diff/zfs_diff_001_pos.golden $TESTDIR/zfs_diff_output.txt

log_pass "'zfs diff' gave the expected output"
