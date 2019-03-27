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
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)userquota_010_pos.ksh	1.1	09/06/22 SMI"
#
################################################################################
#
# __stc_assertion_start
#
# ID: userquota_010_pos
#
# DESCRIPTION:
#       Check userquota and groupquota be overwrited at same time
#
#
# STRATEGY:
#       1. Set userquota and groupquota to a fs
#       2. write to exceed the userquota size to check the result
#       3. write to exceed the groupquota size to check the result
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

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "overwrite any of the {user|group}quota size, it will fail"

log_note "overwrite to $QFS to make it exceed userquota"
log_must $ZFS set userquota@$QUSER1=$UQUOTA_SIZE $QFS
log_must $ZFS set groupquota@$QGROUP=$GQUOTA_SIZE $QFS

mkmount_writable $QFS
log_must user_run $QUSER1 $MKFILE $UQUOTA_SIZE $QFILE
$SYNC

log_must eval "$ZFS get -p userused@$QUSER1 $QFS >/dev/null 2>&1"
log_must eval "$ZFS get -p groupused@$GROUPUSED $QFS >/dev/null 2>&1"

log_mustnot user_run $QUSER1 $MKFILE 1 $OFILE

log_must $RM -f $QFILE

log_note "overwrite to $QFS to make it exceed userquota"
log_mustnot user_run $QUSER1 $MKFILE $GQUOTA_SIZE $QFILE

log_must eval "$ZFS get -p userused@$QUSER1 $QFS >/dev/null 2>&1"
log_must eval "$ZFS get -p groupused@$GROUPUSED $QFS >/dev/null 2>&1"

log_pass "overwrite any of the {user|group}quota size, it fail as expect"
