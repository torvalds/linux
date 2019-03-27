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
# Copyright 2013 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_clear_004_pos
#
# DESCRIPTION:
# Verify 'zpool clear' can clear errors on spare devices.
#
# We don't need to check whether 'zpool clear' actually clears error counters.
# zpool_clear_001_pos will do that.  We just need to check that it doesn't
# return an error when used on a spare vdev.  This is really a test for whether
# zpool_find_vdev() from libzfs can work on a spare vdev.  Note that we're
# talking about he mirror-like "spare-0" vdev, not the leaf hotspare vdev.
#
# STRATEGY:
# 1. Create a pool
# 2. Activate a spare
# 3. Verify that "zpool clear" on the spare returns no errors
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2013-06-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1

        for file in `$LS $TMPDIR/file.*`; do
		log_must $RM -f $file
        done

	restart_zfsd
}


log_assert "Verify 'zpool clear' works on spare vdevs"
log_onexit cleanup

# Stop ZFSD so it won't interfere with our spare device.
stop_zfsd

#make raw files to create various configuration pools
fbase=$TMPDIR/file
log_must create_vdevs $fbase.0 $fbase.1 $fbase.2
VDEV1=$fbase.0
VDEV2=$fbase.1
SDEV=$fbase.2
typeset devlist="$VDEV1 $VDEV2 spare $SDEV"

log_note "'zpool clear' clears leaf-device error."


log_must $ZPOOL create -f $TESTPOOL1 $devlist
log_must $ZPOOL replace $TESTPOOL1 $VDEV1 $SDEV
log_must $ZPOOL clear $TESTPOOL1 "spare-0"

log_pass "'zpool clear' works on spare vdevs"
