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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zvol_misc_006_pos.ksh	1.1	09/01/12 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_006_pos
#
# DESCRIPTION:
# ZFS volume as dump device, it should always have 128k volblocksize
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Use dumpadm set the volume as dump device
# 3. Verify the volume's volblocksize=128k
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-12-01)
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

log_assert "zfs volume as dumpdevice should have 128k volblocksize"

if ! is_dumpswap_supported $TESTPOOL ; then
	log_unsupported "dumpswap not currently supported."
fi
log_onexit cleanup

test_requires DUMPADM

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

typeset oblksize=$($ZFS get -H -o value volblocksize $TESTPOOL/$TESTVOL)
log_note "original $TESTPOOL/$TESTVOL volblocksize=$oblksize"

safe_dumpadm $voldev

typeset blksize=$($ZFS get -H -o value volblocksize $TESTPOOL/$TESTVOL)

if [[ $blksize != "128K" ]]; then
	log_fail "ZFS volume $TESTPOOL/$TESTVOL volblocksize=$blksize"
fi

log_pass "zfs volume as dumpdevice should have 128k volblocksize"
