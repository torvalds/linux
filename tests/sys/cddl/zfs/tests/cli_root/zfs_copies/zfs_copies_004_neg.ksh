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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_copies_004_neg.ksh	1.2	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_copies/zfs_copies.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_copies_004_neg
#
# DESCRIPTION:
# 	Verify that copies cannot be set to other value except for 1, 2 or 3
#
# STRATEGY:
#	1. Create filesystems with copies set as any value other than 1, 2 or 3
#	2. Verify that the create operations fail
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

log_assert "Verify that copies property cannot be set to any value other than 1,2 or 3"

set -A badval 0 01 02 03 0 -1 -2 -3 10 20 30 4 5 6 ? * blah

for val in ${badval[@]}; do
	log_mustnot $ZFS create -o copies=$val $TESTPOOL/$TESTFS1
	log_mustnot $ZFS create -V $VOLSIZE -o copies=$val $TESTPOOL/$TESTVOL1
	log_mustnot $ZFS set copies=$val $TESTPOOL/$TESTFS
	log_mustnot $ZFS set copies=$val $TESTPOOL/$TESTVOL
done
	
log_pass "The copies property cannot be set to any value other than 1,2 or 3 as expected"
