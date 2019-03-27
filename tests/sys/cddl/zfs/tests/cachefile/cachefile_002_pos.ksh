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
# ident	"@(#)cachefile_002_pos.ksh	1.2	09/01/13 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cachefile/cachefile.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: cachefile_002_pos
#
# DESCRIPTION:
#
# Importing a pool with "cachefile" set doesn't update zpool.cache
#
# STRATEGY:
# 1. Create a pool with the cachefile property set
# 2. Verify the pool doesn't have an entry in zpool.cache
# 3. Export the pool
# 4. Import the pool
# 5. Verify the pool does have an entry in zpool.cache
# 6. Export the pool
# 7. Import the pool -o cachefile=<cachefile>
# 8. Verify the pool doesn't have an entry in zpool.cache
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-09-05)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	destroy_pool $TESTPOOL
}

verify_runnable "global"

log_assert "Importing a pool with \"cachefile\" set doesn't update zpool.cache"
log_onexit cleanup

log_must $ZPOOL create -o cachefile=none $TESTPOOL $DISKS
typeset DEVICEDIR=$(get_device_dir $DISKS)
log_mustnot pool_in_cache $TESTPOOL

log_must $ZPOOL export $TESTPOOL
log_must $ZPOOL import -d $DEVICEDIR $TESTPOOL
log_must pool_in_cache $TESTPOOL

log_must $ZPOOL export $TESTPOOL
log_must $ZPOOL import -o cachefile=none -d $DEVICEDIR $TESTPOOL
log_mustnot pool_in_cache $TESTPOOL

log_must $ZPOOL export $TESTPOOL
log_must $ZPOOL import -o cachefile=$CPATH -d $DEVICEDIR $TESTPOOL
log_must pool_in_cache $TESTPOOL

log_pass "Importing a pool with \"cachefile\" set doesn't update zpool.cache"

