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
# ident	"@(#)setup.ksh	1.2	09/05/19 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

verify_runnable "both"

#
# Check if the system support 'send -R'
#
$ZFS send 2>&1 | grep -w "[-R]" > /dev/null 2>&1
if (($? != 0)); then
	log_unsupported
fi

if is_global_zone ; then
	log_must $ZPOOL create $POOL $DISK1
	log_must $ZPOOL create $POOL2 $DISK2
fi
log_must $MKDIR $BACKDIR $TESTDIR

log_must setup_test_model $POOL

log_pass
