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
# ident	"@(#)zfs_unallow_004_pos.ksh	1.2	07/07/31 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unallow_004_pos
#
# DESCRIPTION:
#	Verify '-s' will remove permissions from the named set.
#
# STRATEGY:
#	1. Set @basic set to $ROOT_TESTFS or $ROOT_TESTVOL and allow @basic
#	   to $STAFF1
#	2. Verify $STAFF1 have @basic permissions.
#	3. Verify '-s' will remove permission from the named set.
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

log_assert "Verify '-s' will remove permissions from the named set."
log_onexit restore_root_datasets

for dtst in $DATASETS ; do
	log_must $ZFS allow -s @basic $LOCAL_DESC_SET $dtst
	log_must $ZFS allow -u $STAFF1 @basic $dtst

	log_must verify_perm $dtst $LOCAL_DESC_SET $STAFF1
	log_must $ZFS unallow -s @basic $LOCAL_DESC_SET $dtst
	log_must verify_noperm $dtst $LOCAL_DESC_SET $STAFF1
done

log_pass "Verify '-s' will remove permissions from the named set passed."
