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
# ident	"@(#)zpool_import_010_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_010_pos
#
# DESCRIPTION:
#	'zpool -D -a' can import all the specified directories destroyed pools.
#
# STRATEGY:
#	1. Create a 5 ways mirror pool A with dev0/1/2/3/4, then destroy it.
#	2. Create a stripe pool B with dev1. Then destroy it.
#	3. Create a raidz2 pool C with dev2/3/4. Then destroy it.
#	4. Create a raidz pool D with dev3/4. Then destroy it.
#	5. Create a stripe pool E with dev4. Then destroy it.
#	6. Verify 'zpool import -D -a' recover all the pools.
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
	typeset dt
	for dt in $poolE $poolD $poolC $poolB $poolA; do
		destroy_pool $dt
	done

	log_must $RM -rf $DEVICE_DIR/*
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must create_vdevs ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

log_assert "'zpool -D -a' can import all the specified directories " \
	"destroyed pools."
log_onexit cleanup

poolA=poolA.${TESTCASE_ID}
poolB=poolB.${TESTCASE_ID}
poolC=poolC.${TESTCASE_ID}
poolD=poolD.${TESTCASE_ID}
poolE=poolE.${TESTCASE_ID}

log_must $ZPOOL create $poolA mirror $VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4
log_must $ZPOOL destroy $poolA

log_must $ZPOOL create $poolB $VDEV1
log_must $ZPOOL destroy $poolB

log_must $ZPOOL create $poolC raidz2 $VDEV2 $VDEV3 $VDEV4
log_must $ZPOOL destroy $poolC

log_must $ZPOOL create $poolD raidz $VDEV3 $VDEV4
log_must $ZPOOL destroy $poolD

log_must $ZPOOL create $poolE $VDEV4
log_must $ZPOOL destroy $poolE

log_must $ZPOOL import -d $DEVICE_DIR -D -f -a

for dt in $poolA $poolB $poolC $poolD $poolE; do
	log_must datasetexists $dt
done

log_pass "'zpool -D -a' test passed."
