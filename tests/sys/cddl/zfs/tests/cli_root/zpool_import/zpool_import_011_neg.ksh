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
# ident	"@(#)zpool_import_011_neg.ksh	1.3	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_011_neg
#
# DESCRIPTION:
#	For strip pool, any destroyed pool devices was demaged, zpool import -D
#	will failed.
#
# STRATEGY:
#	1. Create strip pool A with three devices.
#	2. Destroy this pool B.
#	3. Create pool B with one of devices in step 1.
#	4. Verify 'import -D' pool A will failed whenever pool B was destroyed 
#	   or not.
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
	destroy_pool $TESTPOOL2

	#
	# Recreate virtual devices to avoid destroyed pool information on files.
	#
	log_must $RM -rf $VDEV0 $VDEV1 $VDEV2
	log_must create_vdevs $VDEV0 $VDEV1 $VDEV2
}

log_assert "For strip pool, any destroyed pool devices was demaged," \
	"zpool import -D will failed."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
typeset guid=$(get_config $TESTPOOL1 pool_guid)
typeset target=$TESTPOOL1
if (( RANDOM % 2 == 0 )) ; then
	target=$guid
	log_note "Import by guid."
fi
log_must $ZPOOL destroy $TESTPOOL1
log_must $ZPOOL create $TESTPOOL2 $VDEV2

log_mustnot $ZPOOL import -d $DEVICE_DIR -D -f $target

log_must $ZPOOL destroy $TESTPOOL2
log_mustnot $ZPOOL import -d $DEVICE_DIR -D -f $target

log_pass "Any strip pool devices damaged, pool can't be import passed."
