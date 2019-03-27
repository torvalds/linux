#!/usr/local/bin/ksh93 
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
# ident	"@(#)zfs_copies_002_pos.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_copies/zfs_copies.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_copies_002_pos
#
# DESCRIPTION:
# 	Verify that the space used by multiple copies is charged correctly
#
# STRATEGY:
#	1. Create filesystems with copies set as 2,3 respectively;
#	2. Copy specified size data into each filesystem; 
#	3. Verify that the space is charged as expected with zfs list, ls -s, df(1m),
#	   du(1) commands;
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-05-31)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset val
	
	for val in 1 2 3; do
		if datasetexists $TESTPOOL/fs_$val; then
			log_must $ZFS destroy $TESTPOOL/fs_$val
		fi
	done
}

log_assert "Verify that the space used by multiple copies is charged correctly."
log_onexit cleanup

for val in 1 2 3; do
	log_must $ZFS create -o copies=$val $TESTPOOL/fs_$val
	
	log_must $MKFILE $FILESIZE /$TESTPOOL/fs_$val/$FILE
done

#
# Sync up the filesystem
#
$SYNC

#
# Verify 'zfs list' can correctly list the space charged
#
log_note "Verify 'zfs list' can correctly list the space charged."
fsize=${FILESIZE%[m|M]}
for val in 1 2 3; do
	used=$(get_used_prop $TESTPOOL/fs_$val)
	check_used $used $val
done

log_note "Verify 'ls -s' can correctly list the space charged."
for val in 1 2 3; do
	blks=`$LS -lsk /$TESTPOOL/fs_$val/$FILE | $AWK '{print $1}'`
	(( used = blks / 1024 ))
	check_used $used $val
done

log_note "Verify df(1M) can corectly display the space charged."
for val in 1 2 3; do
	used=`$DF -m /$TESTPOOL/fs_$val | $GREP $TESTPOOL/fs_$val \
		| $AWK -v fs=fs_$val '$4 ~ fs {print $3}'`
	check_used $used $val
done

log_note "Verify du(1) can correctly display the space charged."
for val in 1 2 3; do
	used=`$DU -h /$TESTPOOL/fs_$val/$FILE | $AWK '{print $1}'`
	check_used $used $val	
done

log_pass "The space used by multiple copies is charged correctly as expected. "
