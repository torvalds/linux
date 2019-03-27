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
# Copyright 2014 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_hotspare_004_pos
#
# DESCRIPTION: 
#	If two vdevs get removed from a pool with two spares at the same time,
#	both spares will be activated.
#       
#
# STRATEGY:
#	1. Create 1 storage pool with two hot spares.
#	2. Stop the zfsd process.
#	3. Fault two vdevs
#	4. Resume the zfsd process
#	5. Verify that the spares are in use.
#	6. Pause zfsd
#	7. Clear the errors on the faulted vdevs
#	8. Resume zfsd
#	9. Verify that the vdevs ges resilvered and the spares get removed
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
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
	$KILL -s SIGCONT `$PGREP zfsd`
}

function verify_assertion # spare_dev
{
	typeset err_dev1=${devarray[3]}
	typeset err_dev2=${devarray[4]}
	typeset sdev=$1

	$KILL -s SIGSTOP `$PGREP zfsd`

	log_must $ZINJECT -d $err_dev1 -A fault $TESTPOOL
	log_must $ZINJECT -d $err_dev2 -A fault $TESTPOOL
	log_must check_state $TESTPOOL $err_dev1 "FAULTED"
	log_must check_state $TESTPOOL $err_dev2 "FAULTED"

	$KILL -s SIGCONT `$PGREP zfsd`

	# ZFSD can take up to 60 seconds to degrade an array in response to
	# errors (though it's usually faster).  
	for ((timeout=0; $timeout<10; timeout=$timeout+1)); do
		check_state $TESTPOOL "$sdev0" "INUSE"
		cond1=$?
		check_state $TESTPOOL "$sdev1" "INUSE"
		cond2=$?
		if [[ $cond1 -eq 0 && $cond2 -eq 0 ]]; then
			break
		fi
		$SLEEP 6
	done
	log_must $ZPOOL status $TESTPOOL
	log_must check_state $TESTPOOL "$sdev1" "INUSE"
	log_must check_state $TESTPOOL "$sdev2" "INUSE"

	$KILL -s SIGSTOP `$PGREP zfsd`
	$ZPOOL clear $TESTPOOL
	$KILL -s SIGCONT `$PGREP zfsd`

	for ((timeout=0; $timeout<10; timeout=$timeout+1)); do
		check_state $TESTPOOL "$sdev0" "AVAIL"
		cond1=$?
		check_state $TESTPOOL "$sdev1" "AVAIL"
		cond2=$?
		if [[ $cond1 -eq 0 && $cond2 -eq 0 ]]; then
			break
		fi
		$SLEEP 6
	done
	log_must $ZPOOL status $TESTPOOL
	log_must check_state $TESTPOOL "$sdev1" "AVAIL"
	log_must check_state $TESTPOOL "$sdev2" "AVAIL"

	# do cleanup
	destroy_pool $TESTPOOL
}


log_assert "Two simultaneously faulted vdevs will be replaced by available spares"

log_onexit cleanup

ensure_zfsd_running
set_devs
typeset  sdev0="${devarray[0]}"
typeset  sdev1="${devarray[1]}"

set -A my_keywords "mirror" "raidz2"
for keyword in "${my_keywords[@]}" ; do
	setup_hotspares "$keyword"
	verify_assertion $sdev
done

log_pass
