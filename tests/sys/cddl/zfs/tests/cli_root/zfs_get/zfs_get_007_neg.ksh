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
# ident	"@(#)zfs_get_007_neg.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_007_neg
#
# DESCRIPTION:
# 'zfs get -o' should fail with invalid column names 
#
# STRATEGY:
# 1. Run zfs get -o with invalid column name combinations
# 2. Verify that zfs get returns error
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "'zfs get -o' fails with invalid options or column names"

set -A  badargs "o name,property,value,resource" "o name" \
	"-O name,property,value,source" "-oo name" "-o blah" \
	"-o name,property,blah,source" "-o name,name,name,name,name" \
	"-o name,property,value,," "-o *,*,*,*" "-o ?,?,?,?" \
	"-o" "-o ,,,,," "-o -o -o -o" "-o NAME,PROPERTY,VALUE,SOURCE" \
	"-o name,properTy,value,source" "-o name, property, value,source" \
	"-o name:property:value:source" "-o name,property:value,source" \
	"-o name;property;value;source" 

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	log_mustnot eval "$ZFS get \"${badargs[i]}\" >/dev/null 2>&1"

	(( i = i + 1 ))
done 

log_pass "'zfs get -o' fails with invalid options or column name as expected."
