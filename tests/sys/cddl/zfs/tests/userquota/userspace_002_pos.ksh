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
# ident	"@(#)userspace_002_pos.ksh	1.1	09/06/22 SMI"
#

################################################################################
#
# __stc_assertion_start
#
# ID: userspace_002_pos
#
# DESCRIPTION:
#       Check the user used size and quota in zfs userspace
#
#
# STRATEGY:
#       1. set zfs userquota to a fs
#       2. write some data to the fs with specified user and size
#	3. use zfs userspace to check the used size and quota size
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
	if datasetexists $snapfs; then
		log_must $ZFS destroy $snapfs
	fi

	log_must cleanup_quota	
}

log_onexit cleanup

log_assert "Check the zfs userspace used and quota"

log_must $ZFS set userquota@$QUSER1=100m $QFS

mkmount_writable $QFS

log_must user_run $QUSER1 $MKFILE 50m $QFILE
$SYNC

typeset snapfs=$QFS@snap

log_must $ZFS snapshot $snapfs

log_must eval "$ZFS userspace $QFS >/dev/null 2>&1"
log_must eval "$ZFS userspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the quota size in zfs userspace $fs"
	log_must eval "$ZFS userspace $fs | $GREP $QUSER1 | $GREP 100M"

	log_note "check the user used size in zfs userspace $fs"
	log_must eval "$ZFS userspace $fs | $GREP $QUSER1 | $GREP 50.0M"
done

log_pass "Check the zfs userspace used and quota"
