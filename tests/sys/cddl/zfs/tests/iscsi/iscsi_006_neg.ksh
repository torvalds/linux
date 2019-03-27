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
# ident	"@(#)iscsi_006_neg.ksh	1.3	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
# __stc_assertion_start
#
# ID: iscsi_006_neg
#
# DESCRIPTION:
#	Verify iscsioptions can not be changed by zfs command
#
# STRATEGY:
#	1) Save iscsioptions first, then change it on purpose
#	2) Check if the value is really changed
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/$TESTVOL && \
		log_must $ZFS destroy -f $TESTPOOL/$TESTVOL
}

log_onexit cleanup

log_assert "Verify iscsioptions can not be changed by zfs command."

log_must $ZFS create -V $VOLSIZE -o shareiscsi=on $TESTPOOL/$TESTVOL

typeset ioptions
if ! is_iscsi_target $TESTPOOL/$TESTVOL ; then
	log_fail "target is not created."
fi

# Check iscsioptions can not be seen in the output of 'zfs get all'
$ZFS get all $TESTPOOL/$TESTVOL | $GREP iscsioptions
typeset -i ret=$?
[[ $ret -eq 0 ]] && log_fail "iscsioptions can be seen in ' zfs get all'. "

ioptions=$(get_prop iscsioptions $TESTPOOL/$TESTVOL)

$ZFS set iscsioptions="abc" $TESTPOOL/$TESTVOL

if [[ $ioptions != $(get_prop iscsioptions $TESTPOOL/$TESTVOL) ]]; then
	log_fail "iscsioptions property can be changed be $ZFS command."
fi

log_pass "Verify iscsioptions can not be changed by zfs command."
