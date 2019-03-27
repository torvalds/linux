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
# ident	"@(#)cache_002_neg.ksh	1.1	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: cache_001_pos
#
# DESCRIPTION:
# Setting invalid primarycache and secondarycache on file system or volume.
# It should fail.
#
# STRATEGY:
# 1. Create pool, then create filesystem & volume within it.
# 2. Setting invalid {primary|secondary}cache value, it should fail.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"
set -A values  "12345" "null" "not_existed" "abcd1234"

log_assert "Setting invalid {primary|secondary}cache on fs and volume, " \
	"It should fail."

typeset -i i=0
typeset -i j=0
for propname in "primarycache" "secondarycache"
do
	while (( i < ${#dataset[@]} )); do
		j=0
		while (( j < ${#values[@]} )); do
			log_mustnot $ZFS set $propname=${values[j]} ${dataset[i]}
			(( j += 1 ))
		done
		(( i += 1 ))
	done
done

log_pass "Setting invalid {primary|secondary}cache on fs or volume fail as expeced."
