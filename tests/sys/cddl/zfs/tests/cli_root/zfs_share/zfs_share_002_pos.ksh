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
# ident	"@(#)zfs_share_002_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_share_002_pos
#
# DESCRIPTION:
# Verify that "zfs share" with a non-existent file system fails.
#
# STRATEGY:
# 1. Make sure the NONEXISTFSNAME ZFS file system is not in 'zfs list'.
# 2. Invoke 'zfs share <file system>'.
# 3. Verify that share fails
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
		log_must unshare_fs $TESTPOOL/$fs
	done
}

typeset -i ret=0

log_assert "Verify that "zfs share" with a non-existent file system fails."

log_onexit cleanup

log_mustnot $ZFS list $TESTPOOL/$NONEXISTFSNAME 

$ZFS share $TESTPOOL/$NONEXISTFSNAME
ret=$?
(( ret == 1)) || \
	log_fail "'$ZFS share $TESTPOOL/$NONEXISTFSNAME' " \
		"failed with an unexpected return code of $ret."

log_note "Make sure the file system $TESTPOOL/$NONEXISTFSNAME is unshared"
not_shared $TESTPOOL/$NONEXISTFSNAME || \
	log_fail "File system $TESTPOOL/$NONEXISTFSNAME is unexpectedly shared."

log_pass "'$ZFS share' with a non-existent file system fails."
