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
# ident	"@(#)compression_001_pos.ksh	1.3	07/06/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: compression_001_pos
#
# DESCRIPTION:
# Setting a valid compression on file system or volume.
# It should be successful.
#
# STRATEGY:
# 1. Create pool, then create filesystem & volume within it.
# 2. Setting valid value, it should be successful.
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

set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"
set -A values $(get_compress_opts zfs_set)

log_assert "Setting a valid compression on file system and volume, " \
	"It should be successful."

typeset -i i=0
typeset -i j=0
for propname in "compression" "compress"
do
	while (( i < ${#dataset[@]} )); do
		j=0
		while (( j < ${#values[@]} )); do
			set_n_check_prop "${values[j]}" "$propname" "${dataset[i]}"
			(( j += 1 ))
		done
		(( i += 1 ))
	done
done

log_pass "Setting a valid compression on file system or volume pass."
