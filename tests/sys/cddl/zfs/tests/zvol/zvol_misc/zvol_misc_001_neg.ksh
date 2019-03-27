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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zvol_misc_001_neg.ksh	1.3	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_001_neg
#
# DESCRIPTION:
# Verify that using ZFS volume as a dump device fails until 
# dumpswap supported.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Use dumpadm add the volume as dump device
# 3. Verify the return code as expected.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-03-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset dumpdev=$(get_dumpdevice)

	if [[ $dumpdev != $savedumpdev ]] ; then
		safe_dumpadm $savedumpdev
	fi
}

log_assert "Verify that ZFS volume cannot act as dump device until dumpswap supported."
log_onexit cleanup

test_requires DUMPADM

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

if ! is_dumpswap_supported $TESTPOOL ; then
	log_mustnot $DUMPADM -d $voldev
else
	safe_dumpadm $voldev
fi

log_pass "ZFS volume cannot act as dump device until dumpswap supported as expected."
