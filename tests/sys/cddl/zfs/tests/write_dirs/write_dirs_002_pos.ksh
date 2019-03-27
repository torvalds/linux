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
# ident	"@(#)write_dirs_002_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###########################################################################
#
# __stc_assertion_start
#
# ID: write_dirs_002_pos
#
# DESCRIPTION:
# Create as many directories with 5000 files each until the file system
# is full. The zfs file system should be work well and stable.
#
# STRATEGY:
# 1. Create a pool & dateset
# 2. Make directories in the zfs file system
# 3. Create 5000 files in each directories
# 4. Test case exit when the disk is full
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

verify_runnable "both"

function cleanup
{
	for file in `$FIND $TESTDIR -type f`; do
		$CAT /dev/null > $file
	done
	log_must $SYNC
	log_must $RM -rf $TESTDIR/*
}

typeset -i retval=0

log_assert "Creating directories with 5000 files in each, until file system " \
	"is full."

log_onexit cleanup

typeset -i bytes=8192
typeset -i num_writes=20
typeset -i dirnum=50
typeset -i filenum=5000

fill_fs "" $dirnum $filenum $bytes $num_writes
retval=$?
if (( retval == 28 )); then
	log_note "No space left on device."
elif (( retval != 0 )); then
	log_fail "Unexpected exit: $retval"
fi

log_pass "Create many files in a directory succeeded."
