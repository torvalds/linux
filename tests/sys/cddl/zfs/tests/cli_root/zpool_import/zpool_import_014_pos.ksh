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
# Copyright 2013 Spectra Logic  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_014_pos
#
# DESCRIPTION:
#	"'zpool import' can import destroyed disk-backed pools"
#
# STRATEGY:
#	1. Create test pool A.
#	2. Destroy pool A.
#	3. Verify 'import -D' can import pool A.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2013-03-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Destroyed pools are not listed unless with -D option is specified."

log_must $ZPOOL create $TESTPOOL ${DISKS[0]}
log_must $ZPOOL destroy $TESTPOOL
log_mustnot $ZPOOL import $TESTPOOL
log_must $ZPOOL import -D $TESTPOOL
log_must poolexists $TESTPOOL

log_pass
