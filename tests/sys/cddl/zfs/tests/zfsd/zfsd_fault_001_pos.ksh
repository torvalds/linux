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

#
# Copyright (c) 2012,2013 Spectra Logic Corporation.  All rights reserved.
# Use is subject to license terms.
# 
# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfsd_fault_001_pos
#
# DESCRIPTION: 
#   If a vdev experiences IO errors, it will become faulted.
#       
#
# STRATEGY:
#   1. Create a storage pool.  Only use the da driver (FreeBSD's SCSI disk
#      driver) because it has a special interface for simulating IO errors.
#   2. Inject IO errors while doing IO to the pool.
#   3. Verify that the vdev becomes FAULTED.
#   4. ONLINE it and verify that it resilvers and joins the pool.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2012-08-09)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	# Disable error injection, if still active
	sysctl kern.cam.da.$TMPDISKNUM.error_inject=0 > /dev/null

	if poolexists $TESTPOOL; then
		# We should not get here if the test passed.  Print the output
		# of zpool status to assist in debugging.
		$ZPOOL status
		# Clear out artificially generated errors and destroy the pool
		$ZPOOL clear $TESTPOOL
		destroy_pool $TESTPOOL
	fi
}

log_assert "ZFS will fault a vdev that produces IO errors"

log_onexit cleanup
ensure_zfsd_running

# Make sure that at least one of the disks is using the da driver, and use
# that disk for inject errors
typeset TMPDISK=""
for d in $DISKS
do
	b=`basename $d`
	if test ${b%%[0-9]*} == da
	then
		TMPDISK=$b
		TMPDISKNUM=${b##da}
		break
	fi
done
if test -z $TMPDISK
then
	log_unsupported "This test requires at least one disk to use the da driver"
fi


for type in "raidz" "mirror"; do
	log_note "Testing raid type $type"

	# Create a pool on the supplied disks
	create_pool $TESTPOOL $type $DISKS
	log_must $ZFS create $TESTPOOL/$TESTFS

	# Cause some IO errors writing to the pool
	while true; do
		# Running zpool status after every dd operation is too slow.
		# So we will run several dd's in a row before checking zpool
		# status.  sync between dd operations to ensure that the disk
		# gets IO
		for ((i=0; $i<64; i=$i+1)); do
			sysctl kern.cam.da.$TMPDISKNUM.error_inject=1 > \
				/dev/null
			$DD if=/dev/zero bs=128k count=1 >> \
				/$TESTPOOL/$TESTFS/$TESTFILE 2> /dev/null
			$FSYNC /$TESTPOOL/$TESTFS/$TESTFILE
		done
		# Check to see if the pool is faulted yet
		$ZPOOL status $TESTPOOL | grep -q 'state: DEGRADED'
		if [ $? == 0 ]
		then
			log_note "$TESTPOOL got degraded"
			break
		fi
	done

	log_must check_state $TESTPOOL $TMPDISK "FAULTED"

	#find the failed disk guid
	typeset FAILED_VDEV=`$ZPOOL status $TESTPOOL | 
		awk "/^[[:space:]]*$TMPDISK[[:space:]]*FAULTED/ {print \\$1}"`

	# Reattach the failed disk
	$ZPOOL online $TESTPOOL $FAILED_VDEV > /dev/null
	if [ $? != 0 ]; then
		log_fail "Could not reattach $FAILED_VDEV"
	fi

	# Verify that the pool resilvers and goes to the ONLINE state
	for (( retries=60; $retries>0; retries=$retries+1 ))
	do
		$ZPOOL status $TESTPOOL | egrep -q "scan:.*resilvered"
		RESILVERED=$?
		$ZPOOL status $TESTPOOL | egrep -q "state:.*ONLINE"
		ONLINE=$?
		if test $RESILVERED -a $ONLINE
		then
			break
		fi
		$SLEEP 2
	done

	if [ $retries == 0 ]
	then
		log_fail "$TESTPOOL never resilvered in the allowed time"
	fi

	destroy_pool $TESTPOOL
	log_must $RM -rf /$TESTPOOL
done

log_pass
