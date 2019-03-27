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
# Copyright 2016 Spectra Logic  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_missing_005_pos
#
# DESCRIPTION:
#	Verify that a pool can still be imported even if its devices' names
#	have changed, for all types of devices.  This is a test of vdev_geom's
#	import_by_guid functionality.
# STRATEGY:
#	1. Create a supply of file-backed md devices
#	2. Create a disk-backed pool with regular, cache, log, and spare vdevs
#	3. Export it
#	4. Cause all the md devices names to change
#	5. Verify 'zpool import' can import it
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2015-01-4)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Verify that all types of vdevs of a disk-backed exported pool can be imported even if they have been renamed"

# Create md devices so we can control their devnames
# Use high devnames so we'll be unlikely to have collisions
typeset -i REGULAR_U=4000
typeset -i LOG_U=4001
typeset -i CACHE_U=4002
typeset -i SPARE_U=4003
typeset -i REGULAR_ALTU=5000
typeset -i LOG_ALTU=5001
typeset -i CACHE_ALTU=5002
typeset -i SPARE_ALTU=5003
typeset REGULAR=${TMPDIR}/regular
typeset LOG=${TMPDIR}/log
typeset CACHE=${TMPDIR}/cache
typeset SPARE=${TMPDIR}/spare

function cleanup
{
	destroy_pool $TESTPOOL
	$MDCONFIG -d -u $REGULAR_U 2>/dev/null
	$MDCONFIG -d -u $LOG_U 2>/dev/null
	$MDCONFIG -d -u $CACHE_U 2>/dev/null
	$MDCONFIG -d -u $SPARE_U 2>/dev/null
	$MDCONFIG -d -u $REGULAR_ALTU 2>/dev/null
	$MDCONFIG -d -u $LOG_ALTU 2>/dev/null
	$MDCONFIG -d -u $CACHE_ALTU 2>/dev/null
	$MDCONFIG -d -u $SPARE_ALTU 2>/dev/null
	$RM -f $REGULAR
	$RM -f $CACHE
	$RM -f $LOG
	$RM -f $SPARE
}
log_onexit cleanup

log_must $TRUNCATE -s 64m $REGULAR
log_must $TRUNCATE -s 64m $LOG
log_must $TRUNCATE -s 64m $CACHE
log_must $TRUNCATE -s 64m $SPARE
log_must $MDCONFIG -t vnode -a -f $REGULAR -u $REGULAR_U
log_must $MDCONFIG -t vnode -a -f $LOG -u $LOG_U
log_must $MDCONFIG -t vnode -a -f $CACHE -u $CACHE_U
log_must $MDCONFIG -t vnode -a -f $SPARE -u $SPARE_U

log_must $ZPOOL create $TESTPOOL md$REGULAR_U log md$LOG_U cache md$CACHE_U spare md$SPARE_U
log_must $ZPOOL export $TESTPOOL
# Now destroy the md devices, then recreate them with different names
log_must $MDCONFIG -d -u $REGULAR_U
log_must $MDCONFIG -d -u $LOG_U
log_must $MDCONFIG -d -u $CACHE_U
log_must $MDCONFIG -d -u $SPARE_U
log_must $MDCONFIG -t vnode -a -f $REGULAR -u $REGULAR_ALTU
log_must $MDCONFIG -t vnode -a -f $LOG -u $LOG_ALTU
log_must $MDCONFIG -t vnode -a -f $CACHE -u $CACHE_ALTU
log_must $MDCONFIG -t vnode -a -f $SPARE -u $SPARE_ALTU

log_must $ZPOOL import $TESTPOOL
zpool status $TESTPOOL
log_must check_state $TESTPOOL md${REGULAR_ALTU} ONLINE
log_must check_state $TESTPOOL md${LOG_ALTU} ONLINE
log_must check_state $TESTPOOL md${CACHE_ALTU} ONLINE
log_must check_state $TESTPOOL md${SPARE_ALTU} AVAIL

log_pass
