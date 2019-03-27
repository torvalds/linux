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
# Copyright 2017 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_add/zpool_add.kshlib

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
}

log_assert "'zpool add' can add devices, even if a replacing vdev with a spare child is present"

log_onexit cleanup

create_pool $TESTPOOL mirror ${DISK0} ${DISK1}
# A replacing vdev will automatically detach the older member when resilvering
# is complete.  We don't want that to happen during this test, so write some
# data just to slow down resilvering.
$TIMEOUT 60s $DD if=/dev/zero of=/$TESTPOOL/zerofile bs=128k
log_must $ZPOOL replace $TESTPOOL ${DISK0} ${DISK2}
log_must $ZPOOL add $TESTPOOL spare ${DISK3}
log_must $ZPOOL replace $TESTPOOL ${DISK0} ${DISK3}
log_must $ZPOOL add $TESTPOOL spare ${DISK4}

log_pass "'zpool add <pool> <vdev> ...' executes successfully, even when a replacing vdev with a spare child is present"
