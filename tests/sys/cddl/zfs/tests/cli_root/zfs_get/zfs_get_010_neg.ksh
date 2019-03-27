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
# ident	"@(#)zfs_get_010_neg.ksh	1.1	09/06/22 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_list_d.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_010_neg
#
# DESCRIPTION:
# A negative depth or a non numeric depth should fail in 'zfs get -d <n>'
#
# STRATEGY:
# 1. Run zfs get -d with negative depth or non numeric depth
# 2. Verify that zfs get returns error
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-05-22)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

if ! zfs_get_list_d_supported ; then
	log_unsupported "'zfs get -d' is not supported."
fi

log_assert "A negative depth or a non numeric depth should fail in 'zfs get -d <n>'"

set -A  badargs "a" "AB" "aBc" "2A" "a2b" "aB2" "-1" "-32" "-999"

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	log_mustnot eval "$ZFS get -d ${badargs[i]} $TESTPOOL/$TESTFS >/dev/null 2>&1"
	(( i = i + 1 ))
done 

log_pass "A negative depth or a non numeric depth should fail in 'zfs get -d <n>'"


