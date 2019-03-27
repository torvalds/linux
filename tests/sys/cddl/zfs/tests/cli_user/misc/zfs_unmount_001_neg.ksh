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
# ident	"@(#)zfs_unmount_001_neg.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unmount_001_neg
#
# DESCRIPTION:
# Verify that 'zfs umount' and its variants fail as non-root.
#
# STRATEGY:
# 1. Create an array of options.
# 2. Execute each element of the array.
# 3. Verify that the commands fail with an error code.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

set -A args "umount" "umount -f" "unmount" "unmount -f" \
    "umount $TESTPOOL/$TESTFS" "umount -f $TESTPOOL/$TESTFS" \
    "unmount $TESTPOOL/$TESTFS" "unmount -f $TESTPOOL/$TESTFS" \
    "umount $TESTPOOL/$TESTFS@$TESTSNAP" \
    "umount -f $TESTPOOL/$TESTFS@$TESTSNAP" \
    "unmount $TESTPOOL/$TESTFS@$TESTSNAP" \
    "unmount -f $TESTPOOL/$TESTFS@$TESTSNAP" \
    "umount $TESTDIR" "umount -f $TESTDIR" \
    "unmount $TESTDIR" "unmount -f $TESTDIR"
    
log_assert "zfs u[n]mount [-f] [mountpoint|fs|snap]"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot run_unprivileged "$ZFS ${args[i]}"
	((i = i + 1))
done

log_pass "The sub-command 'u[n]mount' fails as non-root."
