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
# ident	"@(#)hotspare_replace_005_pos.ksh	1.0	12/08/09 SL"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_hotspare_003_pos
#
# DESCRIPTION: 
#	If a vdev becomes faulted in a pool with a spare, the spare will be
#	activated.
#       
#
# STRATEGY:
#	1. Create 1 storage pools with hot spares.
#	2. Artificially fault one vdev in the pool to make 1 hotspare in use.
#	3. Verify that the spare is in use.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2012-08-06)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
}

function verify_assertion # spare_dev
{
	typeset err_dev=${devarray[3]}
	typeset sdev=$1

	log_must $ZINJECT -d $err_dev -A degrade $TESTPOOL
	log_must check_state $TESTPOOL $err_dev "DEGRADED"

	# ZFSD can take up to 60 seconds to degrade an array in response to
	# errors (though it's usually faster).  
	for ((timeout=0; $timeout<10; timeout=$timeout+1)); do
		check_state $TESTPOOL "$sdev" "INUSE"
		spare_inuse=$?
		if [[ $spare_inuse == 0 ]]; then
			break
		fi
		$SLEEP 6
	done
	log_must $ZPOOL status $TESTPOOL
	log_must check_state $TESTPOOL "$sdev" "INUSE"

	# do cleanup
	destroy_pool $TESTPOOL
}


log_assert "A faulted vdev will be replaced by an available spare"

log_onexit cleanup

ensure_zfsd_running
set_devs
typeset  sdev="${devarray[0]}"

set -A my_keywords "mirror" "raidz1" "raidz2"
for keyword in "${my_keywords[@]}" ; do
	setup_hotspares "$keyword"
	verify_assertion $sdev
done

log_pass "A faulted vdev will be replaced by an available spare"
