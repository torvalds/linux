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
# ident	"@(#)zvol_misc_005_neg.ksh	1.1	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_005_neg
#
# DESCRIPTION:
# 	Verify a device cannot be dump and swap at the same time.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Set it as swap device.
# 3. Verify dumpadm with this zvol will fail.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-03-10)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	$SWAP -l | $GREP $voldev > /dev/null 2>&1
	if (( $? == 0 )) ; then
		log_must $SWAP -d $voldev
	fi

	typeset dumpdev=$(get_dumpdevice)
	if [[ $dumpdev != $savedumpdev ]] ; then
		safe_dumpadm $savedumpdev
	fi
}

log_assert "Verify a device cannot be dump and swap at the same time."
if ! is_dumpswap_supported $TESTPOOL ; then
	log_unsupported "dumpswap not currently supported."
fi
log_onexit cleanup

test_requires DUMPADM

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

# If device in swap list, it cannot be dump device
log_must $SWAP -a $voldev
log_mustnot $DUMPADM -d $voldev
log_must $SWAP -d $voldev

# If device has dedicated as dump device, it cannot add into swap list
safe_dumpadm $voldev
log_mustnot $SWAP -a $voldev

log_pass "A device cannot be dump and swap at the same time."
