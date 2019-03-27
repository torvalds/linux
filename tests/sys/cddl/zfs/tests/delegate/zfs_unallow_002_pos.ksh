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
# ident	"@(#)zfs_unallow_002_pos.ksh	1.2	07/07/31 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unallow_002_pos
#
# DESCRIPTION:
#	Verify '-d' only remove the permissions on descendent filesystem.

# STRATEGY:
#	1. Set up unallow test model.
#	2. Implement unallow -d to $ROOT_TESTFS
#	3. Verify '-d' only remove the permissions on descendent filesystem.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify '-d' only removed the descendent permissions."
log_onexit restore_root_datasets

log_must setup_unallow_testenv

log_must $ZFS unallow -d $STAFF2 $ROOT_TESTFS
log_must verify_noperm $SUBFS $DESC_SET $STAFF2

log_must $ZFS unallow -d $OTHER1 $ROOT_TESTFS
log_must verify_noperm $SUBFS $LOCAL_DESC_SET $OTHER1
log_must verify_perm $ROOT_TESTFS $LOCAL_DESC_SET $OTHER1

log_must verify_perm $ROOT_TESTFS $LOCAL_DESC_SET $OTHER2
log_must verify_perm $SUBFS $LOCAL_DESC_SET $OTHER2

log_pass "Verify '-d' only removed the descendent permissions passed"
