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
# ident	"@(#)iscsi_005_pos.ksh	1.3	09/01/12 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: iscsi_005_pos
#
# DESCRIPTION:
#	Verify export/import pool with iSCSI
#
# STRATEGY:
#	1) Create a volume, turn on shareiscsi directly on the volume
#	2) Export the pool, check the target is gone after the operation
#	3) Import the pool, check the target is back and its scsi name
#	   not changed
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/$TESTVOL && \
		log_must $ZFS destroy $TESTPOOL/$TESTVOL
}

log_onexit cleanup

log_assert "Verify export/import have right effects on iSCSI targets."

log_must $ZFS create -V $VOLSIZE -o shareiscsi=on $TESTPOOL/$TESTVOL

typeset iname 
if ! is_iscsi_target $TESTPOOL/$TESTVOL ; then
	log_fail "iscsi target is not created."
fi

iname=$(iscsi_name $TESTPOOL/$TESTVOL)

log_must $ZPOOL export $TESTPOOL
if is_iscsi_target $TESTPOOL/$TESTVOL ; then
	log_fail "iscsi target is not removed after the pool is exported."
fi

typeset dir=$(get_device_dir $DISKS)
log_must $ZPOOL import -d $dir -f $TESTPOOL
if ! is_iscsi_target $TESTPOOL/$TESTVOL ; then
	log_fail "iscsi target is not restored after the pool is imported."
fi
if [[ $iname != $(iscsi_name $TESTPOOL/$TESTVOL) ]]; then
	log_fail "The iSCSI name is changed after export/import."
fi

log_pass "Verify export/import have right effects on iSCSI targets."
