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
# ident	"@(#)slog_008_neg.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_008_neg
#
# DESCRIPTION:
#	A raidz/raidz2 log is not supported. 
#
# STRATEGY:
#	1. Try to create pool with unsupported type
#	2. Verify failed to create pool.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "A raidz/raidz2 log is not supported."
log_onexit cleanup

function test_no_raidz_slog # <pooltype> <sparetype>
{
	typeset pooltype=$1
	typeset sparetype=$2

	for logtype in "raidz" "raidz1" "raidz2"; do
		log_mustnot $ZPOOL create $TESTPOOL $type $VDEV \
			$spare $SDEV log $logtype $LDEV $LDEV2
		ldev=$(random_get $LDEV $LDEV2)
		log_mustnot verify_slog_device $TESTPOOL $ldev ONLINE $logtype
		log_must datasetnonexists $TESTPOOL
	done
}
slog_foreach_nologtype test_no_raidz_slog

log_pass "A raidz/raidz2 log is not supported."
