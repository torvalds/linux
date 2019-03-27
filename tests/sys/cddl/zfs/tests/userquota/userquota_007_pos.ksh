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
# ident	"@(#)userquota_007_pos.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userquota_007_pos
#
# DESCRIPTION:
#       
#      userquota/groupquota can be set beyond the fs quota
#      userquota/groupquota can be set at a smaller size than its current usage.
#
# STRATEGY:
#       1. set quota to a fs and set a larger size of userquota and groupquota
#       2. write some data to the fs and set a smaller userquota and groupquota  
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
	log_must cleanup_quota
	log_must $ZFS set quota=none $QFS
}

log_onexit cleanup

log_assert "Check set user|group quota to larger than the quota size of a fs"

log_must $ZFS set quota=200m $QFS
log_must $ZFS set userquota@$QUSER1=500m $QFS
log_must $ZFS set groupquota@$QGROUP=600m $QFS

log_must $ZFS get userquota@$QUSER1 $QFS
log_must $ZFS get groupquota@$QGROUP $QFS

log_note "write some data to the $QFS"
mkmount_writable $QFS
log_must user_run $QUSER1 $MKFILE 100m $QFILE
$SYNC

log_note "set user|group quota at a smaller size than it current usage"
log_must $ZFS set userquota@$QUSER1=90m $QFS
log_must $ZFS set groupquota@$QGROUP=90m $QFS

log_must $ZFS get userquota@$QUSER1 $QFS
log_must $ZFS get groupquota@$QGROUP $QFS

log_pass "set user|group quota to larger than quota size of a fs pass as expect"
