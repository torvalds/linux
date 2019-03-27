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
# ident	"@(#)zfs_get_006_neg.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_006_neg
#
# DESCRIPTION:
# Verify 'zfs get all' can deal with invalid scenarios
#
# STRATEGY:
# 1. Define invalid scenarios for 'zfs get all' 
# 2. Run zfs get with those invalid scenarios
# 3. Verify that zfs get fails with invalid scenarios
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-31)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify 'zfs get all' fails with invalid combination scenarios."

set -f	# Force ksh ignore '?' and '*'
set -A  bad_combine "ALL" "\-R all" "-P all" "-h all" "-rph all" "-RpH all" "-PrH all" \
		"-o all" "-s all" "-? all" "-* all" "-?* all" "all -r" "all -p" \
		"all -H" "all -rp" "all -rH" "all -ph" "all -rpH" "all -r $TESTPOOL" \
		"all -H $TESTPOOL" "all -p $TESTPOOL" "all -r -p -H $TESTPOOL" \
		"all -rph $TESTPOOL" "all,available,reservation $TESTPOOL" \
		"all $TESTPOOL?" "all $TESTPOOL*" "all nonexistpool"

typeset -i i=0
while (( i < ${#bad_combine[*]} ))
do
	log_mustnot eval "$ZFS get ${bad_combine[i]} >/dev/null"

	(( i = i + 1 ))
done 

log_pass "'zfs get all' fails with invalid combinations scenarios as expected."
