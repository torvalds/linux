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
# ident	"@(#)onoffs_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: onoffs_001_pos
#
# DESCRIPTION:
# Setting a valid value to atime, readonly, or setuid on file
# system or volume. It should be successful.
#
# STRATEGY:
# 1. Create pool and filesystem & volume within it.
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

function cleanup
{
	log_must $ZFS mount -a
}

log_onexit cleanup

set -A props "atime" "readonly" "setuid"
set -A values "on" "off"

if is_global_zone ; then
	set -A dataset "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTVOL"
else
	set -A dataset "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR"
fi

log_assert "Setting a valid value to atime, readonly, or setuid on file" \
	"system or volume. It should be successful."

typeset -i i=0
typeset -i j=0
typeset -i k=0
while (( i < ${#dataset[@]} )); do
	j=0
	while (( j < ${#props[@]} )); do
		k=0
		while (( k < ${#values[@]} )); do
			if [[ ${dataset[i]} == "$TESTPOOL/$TESTVOL" &&  \
			    ${props[j]} != "readonly" ]]
			then
				set_n_check_prop "${values[k]}" "${props[j]}" \
				    "${dataset[i]}" "false"
			else
				set_n_check_prop "${values[k]}" "${props[j]}" \
					"${dataset[i]}"
			fi

			(( k += 1 ))
		done
		(( j += 1 ))
	done
	(( i += 1 ))
done

log_pass "Setting a valid value to atime, readonly, or setuid on file" \
	"system or volume pass."
