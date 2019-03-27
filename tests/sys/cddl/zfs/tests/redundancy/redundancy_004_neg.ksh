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
# ident	"@(#)redundancy_004_neg.ksh	1.4	07/05/25 SMI"
#

. $STF_SUITE/tests/redundancy/redundancy.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: redundancy_004_neg
#
# DESCRIPTION:
#	Striped pool have no data redundancy. Any device errors will
#	cause data corruption.
#
# STRATEGY:
#	1. Create N virtual disk file.
#	2. Create stripe pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damage one of the virtual disk file.
#	6. Verify the data is error.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-17)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Verify striped pool have no data redundancy."
log_onexit cleanup

for cnt in 2 3; do
	setup_test_env $TESTPOOL "" $cnt
	damage_devs $TESTPOOL 1 "keep_label"
	log_must $ZPOOL clear $TESTPOOL
	log_mustnot is_pool_healthy $TESTPOOL
done

log_pass "Striped pool has no data redundancy as expected."
