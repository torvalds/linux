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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)refreserv_001_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refreserv_001_pos
#
# DESCRIPTION:
#	Reservations are enforced using the maximum of 'reserv' and 'refreserv'
#
# STRATEGY:
#	1. Setting quota for parent filesystem.
#	2. Setting reservation and refreservation for sub-filesystem.
#	3. Verify the sub-fs reservation are enforced by the maximum of 'reserv'
#	   and 'refreserv'.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-11-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Reservations are enforced using the maximum of " \
	"'reserv' and 'refreserv'"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS ; subfs=$fs/subfs
log_must $ZFS create $subfs
log_must $ZFS set quota=25M $fs

log_must $ZFS set reserv=10M $subfs
log_must $ZFS set refreserv=20M $subfs
mntpnt=$(get_prop mountpoint $fs)
log_mustnot $MKFILE 15M $mntpnt/$TESTFILE

log_must $RM -f $mntpnt/$TESTFILE

log_must $ZFS set reserv=20M $subfs
log_must $ZFS set refreserv=10M $subfs
log_mustnot $MKFILE 15M $mntpnt/$TESTFILE

log_pass "Reservations are enforced using the maximum of " \
	"'reserv' and 'refreserv'"
