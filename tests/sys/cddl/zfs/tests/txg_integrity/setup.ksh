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
# Copyright 2011 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)setup.ksh	1.0	10/20/11 SL"
#

. ${STF_SUITE}/include/libtest.kshlib

# For this test, we create an MD instead of using the defined DISKS.
# Data corrupts much more quickly on an MD.
# Make it small enough that we can tar up the entire pool for post-mortem
# analysis
log_must $MDCONFIG -a -t swap -s 1g -u $TESTCASE_ID

log_must create_pool $TESTPOOL $TESTDEV
$RM -rf $TESTDIR
$MKDIR -p $TESTDIR

log_must $ZFS create $TESTPOOL/$TESTFS
log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

log_pass	


