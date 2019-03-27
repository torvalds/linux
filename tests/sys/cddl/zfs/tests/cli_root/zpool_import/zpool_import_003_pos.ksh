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
# ident	"@(#)zpool_import_003_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_003_pos
#
# DESCRIPTION:
#	Destroyed pools are not listed unless with -D option is specified.
#
# STRATEGY:
#	1. Create test pool A.
#	2. Destroy pool A.
#	3. Verify only 'import -D' can list pool A.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-12)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	log_must $RM $VDEV0 $VDEV1
}

log_assert "Destroyed pools are not listed unless with -D option is specified."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL1 $VDEV0 $VDEV1
log_must $ZPOOL destroy $TESTPOOL1

#
# 'pool:' is the keywords of 'zpool import -D' output.
#
log_mustnot eval "$ZPOOL import -d $DEVICE_DIR | $GREP pool:"
log_must eval "$ZPOOL import -d $DEVICE_DIR -D | $GREP pool:"

log_pass "Destroyed pool only can be listed with -D option."
