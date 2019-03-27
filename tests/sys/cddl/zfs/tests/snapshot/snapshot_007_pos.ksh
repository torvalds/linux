#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)snapshot_007_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: snapshot_007_pos
#
# DESCRIPTION:
# Verify that many snapshots can be made on a zfs dataset.
#
# STRATEGY:
# 1) Create a file in the zfs dataset
# 2) Create a snapshot of the dataset
# 3) Remove all the files from the original dataset
# 4) For each snapshot directory verify consistency
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset -i i=1
	while [ $i -lt $COUNT ]; do
		snapexists $SNAPCTR.$i
		if [[ $? -eq 0 ]]; then
			log_must $ZFS destroy $SNAPCTR.$i
		fi

		if [[ -e $SNAPDIR.$i ]]; then
			log_must $RM -rf $SNAPDIR1.$i > /dev/null 2>&1
		fi

		(( i = i + 1 ))
	done

	if [[ -e $SNAPDIR1 ]]; then
		log_must $RM -rf $SNAPDIR1 > /dev/null 2>&1
	fi

	if [[ -e $TESTDIR ]]; then
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
	fi
}

log_assert "Verify that many snapshots can be made on a zfs dataset."

log_onexit cleanup

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10

log_note "Create some files in the $TESTDIR directory..."
populate_dir $TESTDIR1/file $COUNT $NUM_WRITES $BLOCKSZ ITER $SNAPCTR

log_note "Remove all of the original files"
[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR1/file* > /dev/null 2>&1

i=1
while [[ $i -lt $COUNT ]]; do
	FILECOUNT=`$LS $SNAPDIR1.$i/file* | wc -l`
	typeset j=1
	while [ $j -lt $FILECOUNT ]; do
		log_must $FILE_CHECK $SNAPDIR1.$i/file.$j $j
		(( j = j + 1 ))
	done
	(( i = i + 1 ))
done

log_pass "All files are consistent"
