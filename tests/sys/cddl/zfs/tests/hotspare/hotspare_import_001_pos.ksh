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
# ident	"@(#)hotspare_import_001_pos.ksh	1.4	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_import_001_pos
#
# DESCRIPTION: 
#	If a storage pool has hot spare, 
#	regardless it has been activated or NOT,
#	invoke "zpool export" then import with this storage pool 
#	should runs successfully, and the data should keep integrity
#	after import.
#
# STRATEGY:
#	1. Create a storage pool with hot spares
#	2. Do 'zpool export' then 'zpool import' with following scernarios
#		- the hotspare is only in available list
#		- the hotspare is activated
#		- the hotspare is activated but offline
#		- the hotspare is activated but the basic vdev is offline
#	3. Verify the export/import runs successfully,
#		and the data keep integrity after import
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2006-06-14)
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

function verify_export_import #pool #file #chksum
{
	typeset pool=$1
	typeset file=$2
	typeset checksum1=$3
	typeset -i n=0

	if ! $ZPOOL export $pool; then
		# Rarely, this can fail with EBUSY if the pool's configuration
		# has already changed within the same transaction group.  In
		# that case, it is appropriate to retry.
		while ((n < 3)); do
			$SYNC
			log_note "$ZPOOL busy, retrying export (${n})..."
			if ((n == 2)); then
				log_must $ZPOOL export $pool
			else
				$ZPOOL export $pool && break
			fi
			$SLEEP 1
			n=$((n + 1))
		done
	fi
	log_must $ZPOOL import -d $HOTSPARE_TMPDIR $pool

	[[ ! -e $file ]] && \
		log_fail "$file missing after detach hotspare."
	checksum2=$($SUM $file | $AWK '{print $1}')
	[[ "$checksum1" != "$checksum2" ]] && \
		log_fail "Checksums differ ($checksum1 != $checksum2)"

	return 0
}

function verify_assertion # dev
{
	typeset dev=$1
	typeset odev=${pooldevs[0]}

	#
	#	- the hotspare is activated
	#
	log_must $ZPOOL replace $TESTPOOL $odev $dev
	while ! is_pool_resilvered $TESTPOOL ; do
		$SLEEP 2
	done

	verify_export_import $TESTPOOL \
		$mtpt/$TESTFILE0 $checksum1

	#
	#	- the hotspare is activated
	#	  but the basic vdev is offline
	#
	log_must $ZPOOL offline $TESTPOOL $odev
	verify_export_import $TESTPOOL \
		$mtpt/$TESTFILE0 $checksum1

	log_must $ZPOOL online $TESTPOOL $odev

	log_must $ZPOOL detach $TESTPOOL $dev
}

log_assert "'zpool export/import <pool>' should runs successfully regardless the hotspare is only in list, activated, or offline." 

log_onexit cleanup

typeset mtpt=""

set_devs

checksum1=$($SUM $MYTESTFILE | $AWK '{print $1}')

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword"

	mtpt=$(get_prop mountpoint $TESTPOOL)
	log_must $CP $MYTESTFILE $mtpt/$TESTFILE0

	#
	#	- the hotspare is only in available list
	#
	verify_export_import $TESTPOOL \
		$mtpt/$TESTFILE0 $checksum1

	iterate_over_hotspares verify_assertion "${vdev%% *}"

	log_must $RM -f $mtpt/$TESTFILE0
	destroy_pool "$TESTPOOL"
done

log_pass "'zpool export/import <pool>' should runs successfully regardless the hotspare is only in list, activated, or offline." 
