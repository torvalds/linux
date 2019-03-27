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
# ident	"@(#)iscsi_004_pos.ksh	1.2	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: iscsi_004_pos
#
# DESCRIPTION:
#	Verify renaming a volume does not change target's iSCSI name
#
# STRATEGY:
#	1) Create a volume, turn on shareiscsi directly on the volume
#	2) Save the target's iSCSI name
#	3) Rename the volume, compare the target's iSCSI name with the original
#	   one
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
}

log_onexit cleanup

log_assert "Verify renaming a volume does not change target's iSCSI name."

log_must $ZFS create -V $VOLSIZE -o shareiscsi=on $TESTPOOL/$TESTVOL

typeset iname 
if ! is_iscsi_target $TESTPOOL/$TESTVOL ; then
	log_fail "iscsi target is not created."
fi

iname=$(iscsi_name $TESTPOOL/$TESTVOL)

log_must $ZFS rename $TESTPOOL/$TESTVOL $TESTPOOL/$TESTVOL1

if [[ $iname != $(iscsi_name $TESTPOOL/$TESTVOL1) ]]; then
	log_fail "The iSCSI name is changed after renaming the volume."
fi

log_pass "Verify renaming a volume does not change target's iSCSI name."
