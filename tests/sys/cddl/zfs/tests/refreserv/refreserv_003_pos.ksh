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
# ident	"@(#)refreserv_003_pos.ksh	1.1	08/02/29 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refreserv_003_pos
#
# DESCRIPTION:
#	Verify a snapshot will only be allowed if there is enough free pool 
#	space outside of this refreservation.
#
# STRATEGY:
#	1. Setting quota and refservation
#	2. Verify snapshot can be created, when used =< quota - refreserv
#	3. Verify failed to create snapshot, when used > quota - refreserv
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

log_assert "Verify a snapshot will only be allowed if there is enough " \
	"free space outside of this refreservation."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
log_must $ZFS set quota=25M $fs
log_must $ZFS set refreservation=10M $fs

mntpnt=$(get_prop mountpoint $fs)
log_must $MKFILE 7M $mntpnt/$TESTFILE
log_must $ZFS snapshot $fs@snap

log_must $MKFILE 7M $mntpnt/$TESTFILE.2
log_must $ZFS snapshot $fs@snap2

log_must $MKFILE 7M $mntpnt/$TESTFILE.3
log_mustnot $ZFS snapshot $fs@snap3
if datasetexists $fs@snap3 ; then
	log_fail "ERROR: $fs@snap3 should not exists."
fi

log_pass "Verify a snapshot will only be allowed if there is enough " \
	"free space outside of this refreservation."
