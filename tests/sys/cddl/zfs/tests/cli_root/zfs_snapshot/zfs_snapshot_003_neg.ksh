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
# ident	"@(#)zfs_snapshot_003_neg.ksh	1.1	07/07/31 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_snapshot_003_neg
#
# DESCRIPTION: 
#	"zfs snapshot" fails with bad options,too many arguments or too long 
#	snapshot name
#
# STRATEGY:
#	1. Create an array of invalid arguments
#	2. Execute 'zfs snapshot' with each argument in the array, 
#	3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "'zfs snapshot' fails with bad options, or too many arguments. "

set -A badopts "r" "R" "-R" "-x" "-rR" "-?" "-*" "-123"  

# set too long snapshot name (>256)
l_name="$(gen_dataset_name 260 abcdefg)"

for ds in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCTR $TESTPOOL/$TESTVOL; do
	for opt in ${badopts[@]}; do
		log_mustnot $ZFS snapshot $opt $ds@$TESTSNAP
	done

	log_mustnot $ZFS snapshot $ds@snap $ds@snap1
	log_mustnot $ZFS snapshot -r $ds@snap $ds@snap1

	log_mustnot $ZFS snapshot $ds@$l_name
	log_mustnot $ZFS snapshot -r $ds@$l_name
done

log_pass "'zfs snapshot' fails with bad options or too many arguments as expected." 
