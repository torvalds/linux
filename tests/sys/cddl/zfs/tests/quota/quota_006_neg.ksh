#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)quota_006_neg.ksh	1.1	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: quota_006_neg
#
# DESCRIPTION:
#
# Can't set a quota to less than currently being used by the dataset.
#
# STRATEGY:
# 1) Create a filesystem
# 2) Set a quota on the filesystem that is lower than the space
#	currently in use.
# 3) Verify that the attempt fails.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify cannot set quota lower than the space currently in use"

function cleanup
{
	log_must $ZFS set quota=none $TESTPOOL/$TESTFS
}

log_onexit cleanup

typeset -l quota_integer_size=0
typeset invalid_size="123! @456 7#89 0\$ abc123% 123%s 12%s3 %c123 123%d %x123 12%p3 \
	^def456 789&ghi"
typeset -l space_used=`get_prop used $TESTPOOL/$TESTFS`
(( quota_integer_size = space_used  - 1 ))
typeset -l quota_fp_size=${quota_integer_size}.123

for size in 0 -1 $quota_integer_size -$quota_integer_size $quota_fp_size -$quota_fp_size \
	$invalid_size ; do
	log_mustnot $ZFS set quota=$size $TESTPOOL/$TESTFS
done
log_must $ZFS set quota=$space_used $TESTPOOL/$TESTFS

log_pass "As expected cannot set quota lower than space currently in use"
