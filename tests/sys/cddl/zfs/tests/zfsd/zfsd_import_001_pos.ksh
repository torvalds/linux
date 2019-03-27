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
# Copyright 2013 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfsd_zfsd_002_pos.ksh	1.0	12/08/10 SL"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib
. $STF_SUITE/tests/zfsd/zfsd.kshlib
. $STF_SUITE/include/libgnop.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfsd_import_001_pos
#
# DESCRIPTION: 
#   If a removed drive gets reinserted while the pool is exported, it will
#   replace its spare when reimported.
#
#   This also applies to drives that get reinserted while the machine is
#   powered off.
#       
#
# STRATEGY:
#	1. Create 1 storage pools with hot spares.
#	2. Remove one disk
#	3. Verify that the spare is in use.
#	4. Export the pool
#	5. Recreate the vdev
#	6. Import the pool
#	7. Verify that the vdev gets resilvered and the spare gets removed
#	8. Use additional zpool history data to verify that the pool
#	   finished resilvering _before_ zfsd detached the spare.
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

function verify_assertion # spare_dev
{
	typeset spare_dev=$1
	log_must destroy_gnop $REMOVAL_DISK

	# Check to make sure ZFS sees the disk as removed
	wait_for_pool_removal 20

	# Wait for zfsd to activate the spare
	wait_for_pool_dev_state_change 20 $spare_dev INUSE
	log_must $ZPOOL status $TESTPOOL

	# Export the pool
	log_must $ZPOOL export $TESTPOOL

	# Re-enable the  missing disk
	log_must create_gnop $REMOVAL_DISK

	# Import the pool
	log_must $ZPOOL import $TESTPOOL

	# Check that the disk has rejoined the pool
	wait_for_pool_dev_state_change 20 $REMOVAL_DISK ONLINE

	# Check that the pool resilvered
	while ! is_pool_resilvered $TESTPOOL; do
		$SLEEP 2
	done
	log_must $ZPOOL status $TESTPOOL

	#Finally, check that the spare deactivated
	wait_for_pool_dev_state_change 20 $spare_dev AVAIL

	# Verify that the spare was detached after the scrub was complete
	# Note that resilvers and scrubs are recorded identically in zpool
	# history
	$ZPOOL history -i $TESTPOOL | awk '
		BEGIN {
			scrub_txg=0;
			detach_txg=0
		}
		/scrub done/ {
			split($6, s, "[:\\]]");
			t=s[2];
			scrub_txg = scrub_txg > t ? scrub_txg : t
		}
		/vdev detach/ {
			split($6, s, "[:\\]]");
			t=s[2];
			done_txg = done_txg > t ? done_txg : t
		}
		END {
			print("Scrub completed at txg", scrub_txg);
			print("Spare detached at txg", detach_txg);
			exit(detach_txg > scrub_txg)
		}'
	[ $? -ne 0 ] && log_fail "The spare detached before the resilver completed"
}


log_assert "If a removed drive gets reinserted while the pool is exported, \
	    it will replace its spare when reinserted."

ensure_zfsd_running

typeset REMOVAL_DISK=$DISK0
typeset REMOVAL_NOP=${DISK0}.nop
typeset SPARE_DISK=$DISK4
typeset SPARE_NOP=${DISK4}.nop
typeset OTHER_DISKS="${DISK1} ${DISK2} ${DISK3}"
typeset OTHER_NOPS=${OTHER_DISKS//~(E)([[:space:]]+|$)/.nop\1}
set -A MY_KEYWORDS "mirror" "raidz1" "raidz2"
ensure_zfsd_running
log_must create_gnops $REMOVAL_DISK $OTHER_DISKS $SPARE_DISK
for keyword in "${MY_KEYWORDS[@]}" ; do
	log_must create_pool $TESTPOOL $keyword $REMOVAL_NOP $OTHER_NOPS spare $SPARE_NOP
	verify_assertion
	destroy_pool "$TESTPOOL"
done

log_pass
