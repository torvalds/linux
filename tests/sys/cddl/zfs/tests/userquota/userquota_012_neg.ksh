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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)userquota_012_neg.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_012_neg
#
# DESCRIPTION:
#       userquota and groupquota can not be set against snapshot
#
#
# STRATEGY:
#       1. Set userquota on snap and check the zfs get 
#       2. Set groupquota on snap and check the zfs get 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2009-04-16)
#
# __stc_assertion_end
#
###############################################################################

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

function cleanup
{
	cleanup_quota

	if datasetexists $snap_fs; then
		log_must $ZFS destroy $snap_fs
	fi
}

log_onexit cleanup

typeset snap_fs=$QFS@snap
log_assert "Check  set userquota and groupquota on snapshot"

log_note "Check can not set user|group quuota on snapshot"
log_must $ZFS snapshot $snap_fs

log_mustnot $ZFS set userquota@$QUSER1=$UQUOTA_SIZE $snap_fs

log_mustnot $ZFS set groupquota@$QGROUP=$GQUOTA_SIZE $snap_fs

log_pass "Check  set userquota and groupquota on snapshot"
t"
