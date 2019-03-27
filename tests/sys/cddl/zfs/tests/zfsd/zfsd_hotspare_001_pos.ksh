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
# Copyright 2014 Spectra Logic Corp.  All rights reserved.
# Use is subject to license terms.
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfsd_hotspare_001_pos
#
# DESCRIPTION: 
#	If an active spare fails, it will be replaced by an available spare.
#
# STRATEGY:
#	1. Create a storage pool with two hot spares
#	2. Fail one vdev
#	3. Verify that a spare gets activated
#	4. Fail the spare
#	5. Verify the failed spare was replaced by the other spare.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2014-05-13)
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


log_onexit cleanup

function verify_assertion # type
{
	typeset pool_type=$1

	typeset err_dev=${devarray[3]}
	typeset raidz2_dev="${devarray[4]}"
	typeset mntp=$(get_prop mountpoint $TESTPOOL)
 
	# fail a basic vdev
	$ZINJECT -d $err_dev -A fault $TESTPOOL

	# ZFSD can take up to 60 seconds to replace a failed device
	# (though it's usually faster).  
	for ((timeout=0; $timeout<10; timeout=$timeout+1)); do
		check_state $TESTPOOL "$fail_spare" "INUSE"
		spare_inuse=$?
		if [[ $spare_inuse == 0 ]]; then
			break
		fi
		$SLEEP 6
	done
	log_must $ZPOOL status $TESTPOOL
	log_must check_state $TESTPOOL "$fail_spare" "INUSE"

	# The zpool history should log when a spare device becomes active
	log_must $ZPOOL history -i $TESTPOOL | $GREP "internal vdev attach" | \
	$GREP "spare in vdev=$fail_spare for vdev=$err_dev" > /dev/null

	######################################################################
	# Now fail the active hotspare, and check that the second comes online
	######################################################################

	# fail the spare vdev
	$ZINJECT -d $fail_spare -A fault $TESTPOOL

	# ZFSD can take up to 60 seconds to replace a failed device
	# (though it's usually faster).  
	for ((timeout=0; $timeout<10; timeout=$timeout+1)); do
		check_state $TESTPOOL "$standby_spare" "INUSE"
		spare_inuse=$?
		if [[ $spare_inuse == 0 ]]; then
			break
		fi
		$SLEEP 6
	done
	log_must $ZPOOL status $TESTPOOL

	# The standby spare should be in use, while the original spare should
	# be faulted.
	log_must check_state $TESTPOOL $standby_spare "online"
	log_must check_state $TESTPOOL $standby_spare "INUSE"
	log_mustnot check_state $TESTPOOL $fail_spare "online"

	# The zpool history should log when a spare device becomes active
	log_must $ZPOOL history -i $TESTPOOL | $GREP "internal vdev attach" | \
		$GREP "spare in vdev=$standby_spare for vdev=$fail_spare" > /dev/null

	# do cleanup
	destroy_pool $TESTPOOL
}

log_onexit cleanup

log_assert "An active damaged spare will be replaced by an available spare"

ensure_zfsd_running
set_devs

typeset  fail_spare="${devarray[0]}"
typeset  standby_spare="${devarray[1]}"
typeset  spares="$fail_spare $standby_spare"

set -A my_keywords "mirror" "raidz1" "raidz2"

for keyword in "${my_keywords[@]}"; do
	setup_hotspares "$keyword"
	verify_assertion "$keyword"
done

log_pass "If one of the spare fail, the other available spare will be in use"
