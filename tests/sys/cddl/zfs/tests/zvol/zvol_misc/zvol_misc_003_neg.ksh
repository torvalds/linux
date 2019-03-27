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
# ident	"@(#)zvol_misc_003_neg.ksh	1.2	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_003_neg
#
# DESCRIPTION:
# Verify create storage pool or newfs over volume as dump device is denied.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Use dumpadm set the volume as dump device
# 3. Verify create pool & newfs over the volume return an error.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-01-07)
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

	if poolexists $TESTPOOL1 ; then
		destroy_pool $TESTPOOL1
	fi
}

log_assert "Verify create storage pool or newfs over dump volume is denied."
if ! is_dumpswap_supported $TESTPOOL ; then
	log_unsupported "dumpswap not currently supported."
fi
log_onexit cleanup

test_requires DUMPADM

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

safe_dumpadm $voldev

$ECHO "y" | $NEWFS $voldev > /dev/null 2>&1
if (( $? == 0 )) ; then
	log_fail "newfs over dump volume succeed unexpected"
fi

log_mustnot $ZPOOL create $TESTPOOL1 $voldev

log_pass "Verify create storage pool or newfs over dump volume is denied."
