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
# ident	"@(#)inuse_005_pos.ksh	1.4	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: inuse_005_pos
#
# DESCRIPTION:
# newfs will not interfere with devices and spare devices that are in use 
# by active pool.
#
# STRATEGY:
# 1. Create a with the given disk
# 2. Try to newfs against the disk, verify it fails as expect.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-12-30)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"
set_disks

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
}

log_assert "Verify newfs over active pool fails."

log_onexit cleanup

create_pool $TESTPOOL1 $DISK0
log_mustnot $NEWFS -s 1024 "$DISK0"
destroy_pool $TESTPOOL1

log_pass "Newfs over active pool fails."
