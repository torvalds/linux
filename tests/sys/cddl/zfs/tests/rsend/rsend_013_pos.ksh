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
# ident	"@(#)rsend_013_pos.ksh	1.1	09/01/12 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_013_pos
#
# DESCRIPTION:
#	zfs receive -dF with incremental stream will destroy all the 
#	dataset that not exist on the sender side.
#
# STRATEGY:
#	1. Setup test model
#	2. Send -R @final on pool
#	3. Destroy some dataset within the @final, and create @destroy
#	4. Send -R -I @final @destroy on pool
#	5. Verify receive -dF will destroy all the dataset that not exist
#	   on the sender side.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-12-15)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	cleanup_pool $POOL2
	cleanup_pool $POOL
	log_must setup_test_model $POOL
}

log_assert "zfs receive -dF will destroy all the dataset that not exist" \
	"on the sender side"
log_onexit cleanup

cleanup

#
# Duplicate POOL2 for testing
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -dF $POOL2 < $BACKDIR/pool-final-R"

log_must $ZFS destroy -Rf $POOL/$FS
log_must $ZFS destroy -Rf $POOL/pclone

if is_global_zone ; then
	log_must $ZFS destroy -Rf $POOL/vol
fi
log_must $ZFS snapshot -r $POOL@destroy

log_must eval "$ZFS send -R -I @final $POOL@destroy > " \
	"$BACKDIR/pool-final-destroy-IR"
log_must eval "$ZFS receive -dF $POOL2 < $BACKDIR/pool-final-destroy-IR"

dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

log_pass "zfs receive -dF will destroy all the dataset that not exist" \
	"on the sender side"
