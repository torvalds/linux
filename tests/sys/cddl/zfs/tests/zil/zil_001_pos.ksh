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
# Copyright 2012 Spectra Logic Corporation.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zil/zil.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zil_001_pos
#
# DESCRIPTION:
#
# XXX XXX XXX
#
# STRATEGY:
# 1) XXX
# 2) XXX
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (YYYY-MM-DD) XXX
#
# __stc_assertion_end
#
###############################################################################  

verify_runnable "both"

function cleanup
{
	log_must $LS -lr $TESTDIR
	log_must $RM -rf $TESTDIR/*
}

log_onexit cleanup

log_assert "Verify that basic files and directory operations work"

zil_setup
log_must $TOUCH $TESTDIR/0
log_must $MV $TESTDIR/0 $TESTDIR/1
log_must ln -s $TESTDIR/1 $TESTDIR/2
log_must ln $TESTDIR/1 $TESTDIR/3
log_must $MKDIR $TESTDIR/4
log_must $RMDIR $TESTDIR/4

zil_reimport_pool $TESTPOOL
log_mustnot test -f $TESTDIR/0
log_must test -f $TESTDIR/1
log_must test -L $TESTDIR/2
log_must test -e $TESTDIR/3
log_mustnot test -d $TESTDIR/4

log_pass "Success running basic files and directory operations"
