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
# ident	"@(#)zfs_unallow_001_pos.ksh	1.2	07/07/31 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unallow_001_pos
#
# DESCRIPTION:
#	Verify '-l' only removed the local permissions.
#
# STRATEGY:
#	1. Set up unallow test model.
#	2. Implement unallow -l to $ROOT_TESTFS or $TESTVOL
#	3. Verify '-l' only remove the local permissions.
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

log_assert "Verify '-l' only removed the local permissions."
log_onexit restore_root_datasets

log_must setup_unallow_testenv

for dtst in $DATASETS ; do
	log_must $ZFS unallow -l $STAFF1 $dtst
	log_must verify_noperm $dtst $LOCAL_SET $STAFF1

	log_must $ZFS unallow -l $OTHER1 $dtst
	log_must verify_noperm $dtst $LOCAL_DESC_SET $OTHER1

	log_must verify_perm $dtst $LOCAL_DESC_SET $OTHER2
	if [[ $dtst == $ROOT_TESTFS ]]; then
		log_must verify_perm $SUBFS $LOCAL_DESC_SET $OTHER1 $OTHER2
		log_must verify_perm $SUBFS $DESC_SET $STAFF2
	fi
done

log_pass "Verify '-l' only removed the local permissions passed."
