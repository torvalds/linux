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
# ident	"@(#)truncate_001_pos.ksh	1.3	09/01/12 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: truncate_001_pos
#
# DESCRIPTION:
# 	Tests file truncation within ZFS.
#
# STRATEGY:
# 	1. Open file
# 	2. Write random blocks in random places
# 	3. Truncate the file
# 	4. Repeat steps 2 and 3 lots of times
# 	5. Close the file.
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

function cleanup
{
	[[ -e $TESTDIR ]] && log_must $RM -rf $TESTDIR/*
}

log_assert "Ensure file with random blocks is truncated properly"

options=""
options_display="default options"

log_onexit cleanup

[[ -n "$TRUNC_FILESIZE" ]] && options=" $options -f $TRUNC_FILESIZE "

[[ -n "$TRUNC_BLKSIZE" ]] && options="$options -b $TRUNC_BLKSIZE "

[[ -n "$TRUNC_COUNT" ]] && options="$options -c $TRUNC_COUNT "

[[ -n "$TRUNC_SEED" ]] && options="$options -s $TRUNC_SEED "

[[ -n "$TRUNC_FILEOFFSET" ]] && options="$options -o $TRUNC_FILEOFFSET "

[[ -n "$options" ]] && options_display=$options

log_note "Invoking $FILE_TRUNC with: $options_display"
log_must $FILE_TRUNC $options $TESTDIR/$TESTFILE

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass "Random blocks have been truncated properly."
