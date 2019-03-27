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
# ident	"@(#)enospc_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: enospc_001
#
# DESCRIPTION:
# ENOSPC is returned on an attempt to write a second file
# to a file system after a first file was written that terminated
# with ENOSPC on a cleanly initialized file system.
#
# STRATEGY:
# 1. Write a file until the file system is full.
# 2. Ensure that ENOSPC is returned.
# 3. Write a second file while the file system remains full.
# 4. Verify the return code is ENOSPC.
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

log_assert "ENOSPC is returned when file system is full."
log_must $ZFS set compression=off $TESTPOOL/$TESTFS

log_note "Writing file: $TESTFILE0 until ENOSPC."
$FILE_WRITE -o create -f $TESTDIR/$TESTFILE0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$TESTFILE0 returned: $ret rather than ENOSPC."

log_note "Write another file: $TESTFILE1 but expect ENOSPC."
$FILE_WRITE -o create -f $TESTDIR/$TESTFILE1 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$TESTFILE1 returned: $ret rather than ENOSPC."

log_pass "ENOSPC returned as expected."
