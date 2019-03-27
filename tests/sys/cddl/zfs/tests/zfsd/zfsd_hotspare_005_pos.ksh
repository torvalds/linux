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
# Copyright 2012 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)hotspare_replace_007_pos.ksh	1.0	12/08/10 SL"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfsd_hotspare_005_pos
#
# DESCRIPTION: 
#	If a spare gets added to an already damaged pool, the spare will be
#	activated
#
# STRATEGY:
#	1. Create 1 storage pool without hot spares
#	2. Fail one vdev by using zinject to degrade or fault it
#	3. Verify that it gets degraded or faulted, respectively
#	4. Add a hotspare
#	5. Verify that the spare is in use.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2012-08-10)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL ; then 
		destroy_pool $TESTPOOL
	fi

	partition_cleanup
}

function verify_assertion # damage_type
{
	typeset damage=$1
	typeset err_dev=${devarray[3]}
	typeset mntp=$(get_prop mountpoint $TESTPOOL)

	log_must $ZINJECT -d $err_dev -A $damage $TESTPOOL
	log_must check_state $TESTPOOL $err_dev ${damage_status[$damage]}

	# Add the spare, and check that it is in use
	log_must $ZPOOL add $TESTPOOL spare $sdev
	for ((timeout=0; $timeout<10; timeout=$timeout+1)); do
		if check_state $TESTPOOL "$sdev" "INUSE"; then
			break
		fi
		$SLEEP 6
	done
	log_must $ZPOOL status $TESTPOOL
	log_must check_state $TESTPOOL "$sdev" "INUSE"
}

log_onexit cleanup

log_assert "A spare that is added to a degraded pool will be activated"

ensure_zfsd_running
set_devs

typeset  sdev="${sparedevs[0]}"
typeset -A damage_status
damage_status["degrade"]="DEGRADED"
damage_status["fault"]="FAULTED"

set -A my_keywords "mirror" "raidz1" "raidz2"

for keyword in "${my_keywords[@]}"; do
	for damage in "degrade" "fault"; do
		log_must create_pool $TESTPOOL $keyword ${pooldevs[@]}
		verify_assertion $damage
		destroy_pool $TESTPOOL
	done
done
