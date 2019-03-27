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
# ident	"@(#)devices_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/devices/devices_common.kshlib

################################################################################
#
# __stc_assertion_start 
#
# ID: devices_001_pos
# 
# DESCRIPTION:
# When set property devices=on on file system, devices files can be used in
# this file system. 
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Set devices=on on this file system.
# 3. Separately create block device file and character file.
# 4. Separately read from those two device files.
# 5. Check the return value, and make sure it succeeds.
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

verify_runnable "global"

log_assert "Setting devices=on on file system, the devices files in this file" \
	"system can be used."
log_onexit cleanup

log_must $ZFS set devices=on $TESTPOOL/$TESTFS

#
# Separately create block device file and character device file, then try to
# open them and make sure it succeed.
#
create_dev_file b $TESTDIR/$TESTFILE1
log_must $DD if=$TESTDIR/$TESTFILE1 of=$TESTDIR/$TESTFILE1.out count=1
create_dev_file c $TESTDIR/$TESTFILE2
log_must $DD if=$TESTDIR/$TESTFILE2 of=$TESTDIR/$TESTFILE2.out count=1

log_pass "Setting devices=on on file system and testing it pass."
