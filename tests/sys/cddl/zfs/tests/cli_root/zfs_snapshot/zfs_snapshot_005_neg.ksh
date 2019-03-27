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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_snapshot_005_neg.ksh	1.2	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_snapshot_005_neg
#
# DESCRIPTION:
#	Long name filesystem with snapshot should not break ZFS.
#
# STRATEGY:
#	1. Create filesystem and snapshot.
#	2. When the snapshot length is 256, rename the filesystem.
#	3. Verify it does not break ZFS
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-09)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Verify long name filesystem with snapshot should not break ZFS."

initfs=$TESTPOOL/$TESTFS/$TESTFS
basefs=$initfs
typeset -i ret=0 len snaplen
while ((ret == 0)); do
	$ZFS create $basefs
	$ZFS snapshot $basefs@snap1
	ret=$?
	
	if ((ret != 0)); then
		len=$($ECHO $basefs | $WC -c)
		log_note "The deeply-nested filesystem len: $len"

		#
		# Make sure there are at lease 2 characters left 
		# for snapshot name space, otherwise snapshot name
		# is incorrect
		#
		if ((len >= 255)); then
			if datasetexists $basefs; then
				log_must $ZFS destroy -r $basefs
			fi
			basefs=${basefs%/*}
			len=$($ECHO $basefs| $WC -c)
		fi
		break
	fi

	basefs=$basefs/$TESTFS
done

# Make snapshot name length match the longest one
((snaplen = 256 - len - 1)) # 1: @
snap=$(gen_dataset_name $snaplen "s")
log_must $ZFS snapshot $basefs@$snap

log_mustnot $ZFS rename $basefs ${basefs}a
log_mustnot $ZFS rename $basefs ${basefs}-new
log_mustnot $ZFS rename $initfs ${initfs}-new
log_mustnot $ZFS rename $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS-new

log_pass "Verify long name filesystem with snapshot should not break ZFS."
