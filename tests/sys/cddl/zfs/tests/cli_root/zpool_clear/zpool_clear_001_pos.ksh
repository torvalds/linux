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
# ident	"@(#)zpool_clear_001_pos.ksh	1.3	07/02/06 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_clear_001_pos
#
# DESCRIPTION:
# Verify 'zpool clear' can clear pool errors. 
#
# STRATEGY:
# 1. Create various configuration pools
# 2. Make errors to pool
# 3. Use zpool clear to clear errors
# 4. Verify the errors has been cleared.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-10)
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
}


log_assert "Verify 'zpool clear' can clear errors of a storage pool."
log_onexit cleanup

#make raw files to create various configuration pools
fbase=$TMPDIR/file
log_must create_vdevs $fbase.0 $fbase.1 $fbase.2
set -A poolconf "mirror $fbase.0 $fbase.1 $fbase.2" \
                "raidz1 $fbase.0 $fbase.1 $fbase.2" \
                "raidz2 $fbase.0 $fbase.1 $fbase.2" 

function test_clear
{
	typeset type="$1"
	typeset vdev_arg=""

	log_note "Testing ${type} clear type ..."
	[ "$type" = "device" ] && vdev_arg="${fbase}.0"

	corrupt_file $TESTPOOL1 /$TESTPOOL1/f
	log_must $ZPOOL scrub $TESTPOOL1
	wait_for 20 1 is_pool_scrubbed $TESTPOOL1
	log_must pool_has_errors $TESTPOOL1

	# zpool clear races with things that set error counts; try several
	# times in case that race is hit.
	wait_for 10 1 pool_clear_succeeds $TESTPOOL1 $vdev_arg
}

for devconf in "${poolconf[@]}"; do
	# Create the pool and sync out a file to it.
	log_must $ZPOOL create -f $TESTPOOL1 $devconf
	log_must $FILE_WRITE -o create -f /$TESTPOOL1/f -b 131072 -c 32
	log_must $SYNC /$TESTPOOL1

	test_clear "device"
	test_clear "pool"

	log_must $ZPOOL destroy -f $TESTPOOL1
done

log_pass "'zpool clear' clears pool errors as expected."
