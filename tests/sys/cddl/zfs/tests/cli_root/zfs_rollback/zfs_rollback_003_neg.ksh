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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_rollback_003_neg.ksh	1.5	08/08/15 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rollback_003_neg
#
# DESCRIPTION:
#	Separately verify 'zfs rollback ''|-f|-r|-rf|-R|-rR will fail in 
#	different conditions.
#
# STRATEGY:
#	1. Create pool and file system
#	2. Create 'snap' and 'snap1' of this file system.
#	3. Run 'zfs rollback ""|-f <snap>' and it should fail.
#	4. Create 'clone1' based on 'snap1'.
#	5. Run 'zfs rollback -r|-rf <snap>' and it should fail.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	$PKILL ${DD##*/}
	for snap in $FSSNAP0 $FSSNAP1 $FSSNAP2; do
		if snapexists $snap; then
			log_must $ZFS destroy -Rf $snap
		fi
	done
}

log_assert "Separately verify 'zfs rollback ''|-f|-r|-rf will fail in " \
	"different conditions."
log_onexit cleanup

# Create snapshot1 and snapshot2 for this file system.
#
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP1

# Run 'zfs rollback ""|-f <snap>' and it should fail.
#
log_mustnot $ZFS rollback $TESTPOOL/$TESTFS@$TESTSNAP
log_mustnot $ZFS rollback -f $TESTPOOL/$TESTFS@$TESTSNAP

# Create 'clone1' based on 'snap1'.
#
create_clone $TESTPOOL/$TESTFS@$TESTSNAP1 $TESTPOOL/$TESTCLONE1

# Run 'zfs rollback -r|-rf <snap>' and it should fail.
#
log_mustnot $ZFS rollback -r $TESTPOOL/$TESTFS@$TESTSNAP
log_mustnot $ZFS rollback -rf $TESTPOOL/$TESTFS@$TESTSNAP

log_pass "zfs rollback ''|-f|-r|-rf will fail in different conditions " \
	"passed."
