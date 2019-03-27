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
# ident	"@(#)zfs_list_004_neg.ksh	1.1	07/06/05 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_list_004_neg
#
# DESCRIPTION:
# 	Verify 'zfs list [-r]' should fail while 
#		* the given dataset does not exist
#		* the given path does not exist.
#		* the given path does not belong to zfs.
#
# STRATEGY:
# 1. Create an array of invalid options.
# 2. Execute each element in the array.
# 3. Verify failure is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-05-24)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify 'zfs list [-r]' should fail while the given " \
	"dataset/path does not exist or not belong to zfs."

paths="$TESTPOOL/NONEXISTFS $TESTPOOL/$TESTFS/NONEXISTFS \
	/$TESTDIR/NONEXISTFS /dev"

cd /tmp

for fs in $paths ; do
	log_mustnot run_unprivileged $ZFS list $fs
	log_mustnot run_unprivileged $ZFS list -r $fs
done

log_pass "'zfs list [-r]' fails while the given dataset/path does not exist " \
	"or not belong to zfs."
