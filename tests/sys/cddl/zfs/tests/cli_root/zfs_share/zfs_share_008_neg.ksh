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
# ident	"@(#)zfs_share_008_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_share_008_neg
#
# DESCRIPTION:
# Verify that sharing a dataset other than filesystem fails.
#
# STRATEGY:
# 1. Create a ZFS file system.
# 2. For each dataset in the list, set the sharenfs property.
# 3. Verify that the invalid datasets are not shared.
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

verify_runnable "global"

if is_global_zone ; then
	set -A datasets \
	    "$TESTPOOL/$TESTVOL" "$TESTDIR" 
fi

log_assert "Verify that sharing a dataset other than filesystem fails."

typeset -i i=0
while (( i < ${#datasets[*]} ))
do
	log_mustnot $ZFS set sharenfs=on ${datasets[i]}

	option=`get_prop sharenfs ${datasets[i]}`
	if [[ $option == ${datasets[i]} ]]; then
		log_fail "set sharenfs failed. ($option == ${datasets[i]})"
	fi

	not_shared ${datasets[i]} || \
	    log_fail "An invalid setting '$option' was propagated."

	log_mustnot $ZFS share ${datasets[i]}

	not_shared ${datasets[i]} || \
	    log_fail "An invalid dataset '${datasets[i]}' was shared."

	((i = i + 1))
done

log_pass "Sharing datasets other than filesystems failed as expected."
