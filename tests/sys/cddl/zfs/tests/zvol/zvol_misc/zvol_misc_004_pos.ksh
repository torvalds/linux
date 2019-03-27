#! /usr/local/bin/ksh93 -p
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zvol_misc_004_pos.ksh	1.1	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zvol_misc_004_pos
#
# DESCRIPTION:
# Verify permit to create snapshot over active dumpswap zvol.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Set the volume as dump or swap
# 3. Verify create snapshot over the zvol succeed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-01-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset dumpdev=$(get_dumpdevice)
	if [[ $dumpdev != $savedumpdev ]] ; then
		safe_dumpadm $savedumpdev
	fi

	$SWAP -l | $GREP -w $voldev > /dev/null 2>&1
        if (( $? == 0 ));  then
		log_must $SWAP -d $voldev
	fi

	typeset snap
	for snap in snap0 snap1 ; do
		if datasetexists $TESTPOOL/$TESTVOL@$snap ; then
			log_must $ZFS destroy $TESTPOOL/$TESTVOL@$snap
		fi
	done
}

function verify_snapshot
{
	typeset volume=$1

	log_must $ZFS snapshot $volume@snap0
	log_must $ZFS snapshot $volume@snap1
	log_must datasetexists $volume@snap0 $volume@snap1

	log_must $ZFS destroy $volume@snap1
	log_must $ZFS snapshot $volume@snap1

	log_mustnot $ZFS rollback -r $volume@snap0
	log_must datasetexists $volume@snap0
	log_must datasetexists $volume@snap1

	log_must $ZFS destroy -r $volume@snap0
}

log_assert "Verify permit to create snapshot over dumpswap."
if ! is_dumpswap_supported $TESTPOOL ; then
	log_unsupported "dumpswap not currently supported."
fi
log_onexit cleanup

test_requires DUMPADM

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

# create snapshot over dump zvol
safe_dumpadm $voldev
log_must is_zvol_dumpified $TESTPOOL/$TESTVOL

verify_snapshot $TESTPOOL/$TESTVOL

safe_dumpadm $savedumpdev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

# create snapshot over swap zvol

log_must $SWAP -a $voldev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

verify_snapshot $TESTPOOL/$TESTVOL

log_must $SWAP -d $voldev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

log_pass "Create snapshot over dumpswap zvol succeed."
