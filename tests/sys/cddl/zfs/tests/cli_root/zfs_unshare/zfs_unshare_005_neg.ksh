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
# ident	"@(#)zfs_unshare_005_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unshare_005_neg
#
# DESCRIPTION:
# Verify that unsharing a dataset and mountpoint other than filesystem fails.
#
# STRATEGY:
# 1. Create a volume, dataset other than a ZFS file system
# 2. Verify that the datasets other than file system are not support by 'zfs unshare'.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

set -A datasets \
	"$TESTPOOL" "$ZFSROOT/$TESTPOOL" \
	"$TESTPOOL/$TESTCTR" "$ZFSROOT/$TESTPOOL/$TESTCTR" \
    	"$TESTPOOL/$TESTVOL" "/dev/zvol/$TESTPOOL/$TESTVOL"

log_assert "Verify that unsharing a dataset other than filesystem fails."

typeset -i i=0
while (( i < ${#datasets[*]} ))
do
	log_mustnot $ZFS unshare ${datasets[i]}

	((i = i + 1))
done

log_pass "Unsharing datasets other than filesystem failed as expected."
