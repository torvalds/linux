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
# ident	"@(#)zfs_mount_002_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_mount_002_pos
#
# DESCRIPTION:
# Invoking "zfs mount <filesystem>" with a filesystem whose name is not in 
# "zfs list", will fail with a return code of 1.
#
# STRATEGY:
# 1. Make sure the NONEXISTFSNAME ZFS filesystem is not in 'zfs list'.
# 2. Invoke 'zfs mount <filesystem>'.
# 3. Verify that mount failed with return code of 1.
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
	typeset fs
	for fs in $NONEXISTFSNAME $TESTFS ; do
		log_must force_unmount $TESTPOOL/$fs
	done
}


log_assert "Verify that '$ZFS $mountcmd' with a filesystem " \
	"whose name is not in 'zfs list' will fail with return code 1."

log_onexit cleanup

log_note "Make sure the filesystem $TESTPOOL/$NONEXISTFSNAME " \
	"is not in 'zfs list'"
log_mustnot $ZFS list $TESTPOOL/$NONEXISTFSNAME 

typeset -i ret=0
$ZFS $mountcmd $TESTPOOL/$NONEXISTFSNAME
ret=$?
(( ret == 1 )) || \
	log_fail "'$ZFS $mountcmd $TESTPOOL/$NONEXISTFSNAME' " \
		"unexpected return code of $ret."

log_note "Make sure the filesystem $TESTPOOL/$NONEXISTFSNAME is unmounted"
unmounted $TESTPOOL/$NONEXISTFSNAME || \
	log_fail Filesystem $TESTPOOL/$NONEXISTFSNAME is mounted

log_pass "'$ZFS $mountcmd' with a filesystem " \
	"whose name is not in 'zfs list' failed with return code 1."
