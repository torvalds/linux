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
# ident	"@(#)zfs_set_001_neg.ksh	1.3	07/02/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_set_001_neg
#
# DESCRIPTION:
# Setting invalid value to mountpoint, checksum, atime, readonly, setuid,
# or canmount on a file system, volume. It should be failed.
#
# STRATEGY:
# 1. Create pool, then create file system & volume within it.
# 2. Setting invalid value, it should be failed.
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

set -A props "" "mountpoint" "checksum" "compression" "atime" "readonly" \
	"setuid" "canmount"

set -A values "" "mountpoint" "checksum" "compression" "atime" "readonly" \
	"setuid" "0" "-?" "-on" "--on" "*" "?" "Legacy" "NONE" "oN" \
	"On" "ON" "ofF" "OFf" "oFF" "Off" "OfF" "OFF" "LzJb" "lZJb" "LZjB" \
	"blad" "default" "TESTPOOL" "$TESTPOOL/$TESTCTR" \
	"$TESTPOOL/$TESTCTR/$TESTFS" "$TESTPOOL/$TESTFS" 
set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"

log_assert "Setting invalid value to mountpoint, checksum, compression, atime,"\
	"readonly, setuid, or canmount on a file system file system or volume." \
	"It should be failed."

typeset -i i=0
typeset -i j=0
typeset -i k=0
while (( i < ${#dataset[@]} )); do
	j=0
	while (( j < ${#props[@]} )); do
		k=0
		while (( k < ${#values[@]} )); do
			set_n_check_prop "${values[k]}" "${props[j]}" \
				"${dataset[i]}" false
			(( k += 1 ))
		done
		(( j += 1 ))
	done
	(( i += 1 ))
done

log_pass "Setting invalid value to mountpoint, checksum, compression, atime, " \
	"readonly, setuid, or canmount on file system or volume pass."
