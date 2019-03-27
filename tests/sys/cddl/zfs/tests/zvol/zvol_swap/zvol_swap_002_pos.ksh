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
# ident	"@(#)zvol_swap_002_pos.ksh	1.4	09/05/19 SMI"
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_swap_002_pos
#
# DESCRIPTION:
# Using a zvol as swap space, fill with files until ENOSPC returned.
#
# STRATEGY:
# 1. Create a pool
# 2. Create a zvol volume
# 3. Add zvol to swap space
# 4. Fill swap space until ENOSPC is returned
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

log_unsupported "Fill swap will cause system hang, hide this case temporarily."

verify_runnable "global"

function cleanup
{
	$RM -rf $TMPDIR/$TESTDIR
	
	if is_swap_inuse $voldev ; then
		log_must $SWAP -d $voldev
	fi
}

log_assert "Using a zvol as swap space, fill with files until ENOSPC returned."

if ! is_dumpswap_supported $TESTPOOL ; then
	log_unsupported "ZVOLs as swap devices are not currently supported."
fi

log_onexit cleanup

voldev=/dev/zvol/$TESTPOOL/$TESTVOL

$SWAP -l | $GREP zvol
if (( $? != 0 )) ; then
	log_note "Add zvol volume as swap space"
	log_must $SWAP -a $voldev
fi

typeset -i filenum=0
typeset -i retval=0
typeset testdir=$TMPDIR/$TESTDIR

log_note "Attempt to fill $TMPDIR until ENOSPC is hit"
fill_fs $testdir -1 100 $BLOCKSZ $NUM_WRITES
retval=$?

(( $retval != $ENOSPC )) && \
    log_fail "ENOSPC was not returned, $retval was returned instead"
    
log_pass "ENOSPC was returned as expected"
