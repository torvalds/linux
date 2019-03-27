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
# ident	"@(#)zpool_003_pos.ksh	1.1	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_003_pos
#
# DESCRIPTION:
#	Verify debugging features of zpool such as ABORT and freeze/unfreeze
#	should run successfully.
#
# STRATEGY:
# 1. Create an array containg each zpool options.
# 2. For each element, execute the zpool command.
# 3. Verify it run successfully.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Debugging features of zpool should succeed."

log_must $ZPOOL -? > /dev/null 2>&1

if is_global_zone ; then
	log_must $ZPOOL freeze $TESTPOOL
else
	log_mustnot $ZPOOL freeze $TESTPOOL
	log_mustnot $ZPOOL freeze ${TESTPOOL%%/*}
fi

log_mustnot $ZPOOL freeze fakepool	

ZFS_ABORT=1; export ZFS_ABORT
$ZPOOL > /dev/null 2>&1
typeset ret=$?
unset ZFS_ABORT
# Note: "/bin/kill -l $ret" will not recognize the signal number.  We must use
# ksh93's builtin kill command
if [ `kill -l $ret` != "ABRT" ]; then
	log_fail "$ZPOOL not dump core by request."
fi

log_pass "Debugging features of zpool succeed."
