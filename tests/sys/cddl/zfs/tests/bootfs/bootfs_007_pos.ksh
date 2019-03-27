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
# ident	"@(#)bootfs_007_neg.ksh	1.3	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  bootfs_007_neg
#
# DESCRIPTION:
#
# Setting bootfs on a pool which was configured with the whole disk
# (i.e. EFI) will succeed.  This is only supported on FreeBSD, not Solaris.
#
# STRATEGY:
# 1. create a pool with a whole disk
# 2. create a filesystem on this pool
# 3. verify we can set bootfs on the filesystem we just created.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-07-08)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup {
	if poolexists $TESTPOOL ; then
		destroy_pool "$TESTPOOL"
	fi
}

log_onexit cleanup

DISK=${DISKS%% *}
typeset EFI_BOOTFS=$TESTPOOL/efs
typeset assert_mesg="setting bootfs on a pool which was configured with the \
		     whole disk will succeed"
		     
log_assert $assert_mesg
create_pool "$TESTPOOL" "$DISK"
log_must $ZFS create $EFI_BOOTFS

log_must $ZPOOL set bootfs=$EFI_BOOTFS $TESTPOOL

log_pass $assert_mesg
