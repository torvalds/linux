#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)zvol_ENOSPC_001_pos.ksh	1.2	07/01/09 SMI"
#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_ENOSPC_001_pos
#
# DESCRIPTION:
# A zvol volume will return ENOSPC when the underlying pool runs out of
# space.
#
# STRATEGY:
# 1. Create a pool
# 2. Create a zvol volume
# 3. Create a ufs file system ontop of the zvol
# 4. Mount the ufs file system
# 5. Fill volume until ENOSPC is returned
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

verify_runnable "global"

function cleanup
{
	# unmounting the UFS filesystem can take more than 60s, and Kyua has a
	# hardcoded 60s limit for the cleanup phase.  So we must unmount the
	# filesystem here rather than cleanup.ksh.
	ismounted $TESTDIR ufs && log_must $UMOUNT -f $TESTDIR
	$RMDIR $TESTDIR
}

log_assert "A zvol volume will return ENOSPC when the underlying pool " \
    "runs out of space."

log_onexit cleanup

typeset -i fn=0
typeset -i retval=0

log_mustbe ENOSPC fill_fs $TESTDIR -1 50 $BLOCKSZ $NUM_WRITES

log_pass "ENOSPC was returned as expected"
