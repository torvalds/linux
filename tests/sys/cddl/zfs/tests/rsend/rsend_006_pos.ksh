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
# ident	"@(#)rsend_006_pos.ksh	1.1	08/02/27 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_006_pos
#
# DESCRIPTION:
#	Rename snapshot name will not change the dependent order.
#
# STRATEGY:
#	1. Set up a set of datasets.
#	2. Rename part of snapshots.
#	3. Send -R all the POOL
#	4. Verify snapshot name will not change the dependent order.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

#		Source			Target
#
set -A	snaps	"$POOL@init"		"$POOL@snap0"	\
		"$POOL@snapA"		"$POOL@snap1"	\
		"$POOL@snapC"		"$POOL@snap2"	\
		"$POOL@final"		"$POOL@init"

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must setup_test_model $POOL
}

log_assert "Rename snapshot name will not change the dependent order."
log_onexit cleanup

typeset -i i=0
while ((i < ${#snaps[@]})); do
	log_must $ZFS rename -r ${snaps[$i]} ${snaps[((i+1))]}

	((i += 2))
done

#
# Duplicate POOL2 for testing
#
log_must eval "$ZFS send -R $POOL@init > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-final-R"

dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

log_pass "Rename snapshot name will not change the dependent order."
