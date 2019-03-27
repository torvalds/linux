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
# ident	"@(#)refreserv_005_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: refreserv_005_pos
#
# DESCRIPTION:
#	Volume refreservation is limited by volsize
#
# STRATEGY:
#	1. Create volume on filesystem
#	2. Setting quota for parenet filesytem
#	3. Verify volume refreservation is only limited by volsize
#	4. Verify volume refreservation can be changed when volsize changed
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-11-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Volume refreservation is limited by volsize"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS; vol=$fs/vol
log_must $ZFS create -V 10M $vol

# Verify the parent filesystem does not affect volume
log_must $ZFS set quota=25M $fs
log_must $ZFS set refreservation=10M $vol
avail=$(get_prop mountpoint $vol)
log_mustnot $ZFS set refreservation=$avail $vol

# Verify it is affected by volsize
log_must $ZFS set volsize=15M $vol
log_must $ZFS set refreservation=15M $vol
log_mustnot $ZFS set refreservation=16M $vol

log_pass "Volume refreservation is limited by volsize"
