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
# ident	"@(#)zfs_rename_012_neg.ksh	1.1	07/07/31 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_rename_012_neg
#
# DESCRIPTION:
#	'zfs rename' should be failed with bad option, null target dataset,  
#	too many datasets and long target dataset name.
#
# STRATEGY:
#	1. Create a set of ZFS datasets;
#	2. Try 'zfs rename' with various illegal scenarios;
#	3. Verify 'zfs rename' command should be failed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-15)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "'zfs rename' should fail with bad option, null target dataset and" \
		"too long target dataset name."

set -A badopts "r" "R" "-R" "-rR" "-Rr" "-P" "-pP" "-Pp" "-r*" "-p*" "-?" "-*" \
		"-" "-o"
set -A datasets "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTFS@$TESTSNAP" \
		"$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTCTR/$TESTFS1" "$TESTPOOL/$TESTVOL"

longname="$(gen_dataset_name 260 abcdefg)"	

log_must $ZFS snapshot $TESTPOOL/$TESTFS@$TESTSNAP
for ds in ${datasets[@]}; do
	for opt in ${badopts[@]}; do
		log_mustnot $ZFS rename $opt $ds ${ds}-new
	done
	log_mustnot $ZFS rename $ds
	log_mustnot $ZFS rename $ds ${ds}-new ${ds}-new1
	log_mustnot $ZFS rename $ds ${ds}.$longname
done

log_pass "'zfs rename' fails with illegal scenarios as expected."
