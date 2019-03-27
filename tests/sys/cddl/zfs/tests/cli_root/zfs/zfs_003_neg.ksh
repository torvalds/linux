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
# ident	"@(#)zfs_003_neg.ksh	1.1	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_003_neg
#
# DESCRIPTION:
# zfs command will failed with unexpected scenarios:
# (1) ZFS_DEV cannot be opened
# (2) MNTTAB cannot be opened
#
# STRATEGY:
# 1. Create an array of zfs command
# 2. Execute each command in the array
# 3. Verify the command aborts and generate a core file 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "zfs fails with unexpected scenarios." 

#verify zfs failed if ZFS_DEV cannot be opened
ZFS_DEV=/dev/zfs
MNTTAB=/etc/mnttab

for file in $ZFS_DEV $MNTTAB; do
	if [[ -e $file ]]; then
		$MV $file ${file}.bak
	fi
	for cmd in "" "list" "get all" "mount"; do
		log_mustnot eval "$ZFS $cmd >/dev/null 2>&1"
	done 
	$MV ${file}.bak $file
done

log_pass "zfs fails with unexpected scenarios as expected."
