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
# ident	"@(#)zfs_allow_009_neg.ksh	1.1	07/01/09 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_009_neg
#
# DESCRIPTION:
#	zfs allow can deal with invalid arguments.(Invalid options or combination)
#
# STRATEGY:
#	1. Verify invalid argumets will cause error.
#	2. Verify non-optional argument was missing will cause error.
#	3. Verify invalid options cause error.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify invalid arguments are handled correctly."
log_onexit restore_root_datasets

# Permission sets are limited to 64 characters in length.
longset="set123456789012345678901234567890123456789012345678901234567890123"
for dtst in $DATASETS ; do
	log_mustnot eval "$ZFS allow -s @$longset $dtst"
	# Create non-existent permission set
	typeset timestamp=$($DATE +'%F-%R:%S')
	log_mustnot $ZFS allow -s @non-existent $dtst
	log_mustnot $ZFS allow $STAFF "atime,created,mounted" $dtst
	log_mustnot $ZFS allow $dtst $TESTPOOL
	log_mustnot $ZFS allow -c $dtst
	log_mustnot $ZFS allow -u $STAFF1 $dtst
	log_mustnot $ZFS allow -u $STAFF1 -g $STAFF_GROUP "create,destroy" $dtst
	log_mustnot $ZFS allow -u $STAFF1 -e "mountpoint" $dtst
done

log_pass "Invalid arguments are handled correctly."
