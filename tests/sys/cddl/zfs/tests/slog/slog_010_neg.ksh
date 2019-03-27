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
# ident	"@(#)slog_010_neg.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/tests/slog/slog.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: slog_010_neg
#
# DESCRIPTION:
#	Slog device can not be replaced with spare device
#
# STRATEGY:
#	1. Create a pool with hotspare and log devices.
#	2. Verify slog device can not be replaced with hotspare device.
#	3. Create pool2 with hotspare
#	4. Verify slog device can not be replaced with hotspare device in pool2.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Slog device can not be replaced with spare device."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL $VDEV spare $SDEV log $LDEV
sdev=$(random_get $SDEV)
ldev=$(random_get $LDEV)
log_mustnot $ZPOOL replace $TESTPOOL $ldev $sdev
log_mustnot verify_slog_device $TESTPOOL $sdev 'ONLINE'
log_must verify_slog_device $TESTPOOL $ldev 'ONLINE'

log_must $ZPOOL create $TESTPOOL2 $VDEV2 spare $SDEV2
sdev2=$(random_get $SDEV2)
log_mustnot $ZPOOL replace $TESTPOOL $ldev $sdev2
log_mustnot $ZPOOL replace -f $TESTPOOL $ldev $sdev2
log_mustnot verify_slog_device $TESTPOOL $sdev2 'ONLINE'
log_must verify_slog_device $TESTPOOL $ldev 'ONLINE'

log_pass "Slog device can not be replaced with spare device."
