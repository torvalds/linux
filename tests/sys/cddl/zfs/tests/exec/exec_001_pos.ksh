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
# ident	"@(#)exec_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start 
#
# ID: exec_001_pos
# 
# DESCRIPTION:
# When set property exec=on on a filesystem, processes can be executed from
# this filesystem.
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Copy '/bin/ls' to the ZFS file system.
# 3. Setting exec=on on this file system.
# 4. Make sure '/bin/ls' can work in this ZFS file system.
# 5. Make sure mmap which is using the PROT_EXEC calls succeed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-11)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $RM $TESTDIR/myls
}

log_assert "Setting exec=on on a filesystem, processes can be executed from " \
	"this file system."
log_onexit cleanup

log_must $CP $LS $TESTDIR/myls
log_must $ZFS set exec=on $TESTPOOL/$TESTFS
log_must $TESTDIR/myls
log_must mmap_exec $TESTDIR/myls

log_pass "Setting exec=on on filesystem testing passed."
