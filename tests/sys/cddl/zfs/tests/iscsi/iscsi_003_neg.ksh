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
# ident	"@(#)iscsi_003_neg.ksh	1.2	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: iscsi_003_neg
#
# DESCRIPTION:
#	Verify invalid value of shareiscsi can not be set
#
# STRATEGY:
#	1) verify a set of invalid value of shareiscsi can not be set
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
	if [[ "off" != $(get_prop shareiscsi $TESTPOOL/$TESTFS) ]]; then
		$ZFS set shareiscsi=off $TESTPOOL/$TESTFS
	fi
}

log_onexit cleanup

log_assert "Verify invalid value of shareiscsi can not be set"

set -A inval_str "ON" "oN" "oFF" "Off" "0ff" "disK" "tape" "abc" "??" \
		"type=abc" "type=DISk" "type=type" "TYPE=disk" \
		"type=on" "type=off" "type=123"

typeset str

for str in ${inval_str[@]}; do
	log_mustnot $ZFS set shareiscsi=$str $TESTPOOL/$TESTFS
done

log_pass "Verify invalid value of shareiscsi can not be set"
