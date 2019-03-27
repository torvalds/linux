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
# ident	"@(#)iscsi_001_pos.ksh	1.2	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: iscsi_001_pos
#
# DESCRIPTION:
#	Verify setting shareiscsi property on volume will make it an iSCSI
#	target	
#
# STRATEGY:
#	1) Create a volume, turn on shareiscsi directly on the volume
#	2) Check if the target is created or not
#	3) Destroy the volume, then turn on shareiscsi property on parent
#	   filesystem at first
#	4) Then create the volume, check if the target is created or not
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
	datasetexists $TESTPOOL/$TESTVOL1 && \
		log_must $ZFS destroy $TESTPOOL/$TESTVOL1
	if [[ "off" != $(get_prop shareiscsi $TESTPOOL) ]]; then
		log_must $ZFS set shareiscsi=off $TESTPOOL
	fi
}

log_onexit cleanup

log_assert "Verify that setting shareiscsi property on volume will make it \
	an iSCSI target as expected."


# Check shareiscsi property directly on volume at first
log_must $ZFS set shareiscsi=off $TESTPOOL
log_must $ZFS create -V $VOLSIZE $TESTPOOL/$TESTVOL
log_must $ZFS set shareiscsi=on $TESTPOOL/$TESTVOL
if ! is_iscsi_target $TESTPOOL/$TESTVOL ; then
	log_fail "iscsi target is not created via directly turning on \
			shareiscsi property on volume"
fi

# Check setting shareiscsi property on parent filesystem also have
# effects on volume
log_must $ZFS set shareiscsi=on $TESTPOOL
log_must $ZFS create -V $VOLSIZE $TESTPOOL/$TESTVOL1
if ! is_iscsi_target $TESTPOOL/$TESTVOL1 ; then
	log_fail "iscsi target is not created via turning on \
		shareiscsi property on parent filesystem"
fi

log_pass "Verify that setting shareiscsi property on volume will make it \
	an iSCSI target as expected."
