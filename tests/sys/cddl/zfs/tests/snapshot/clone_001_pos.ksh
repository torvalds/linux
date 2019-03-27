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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)clone_001_pos.ksh	1.6	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: clone_001_pos
#
# DESCRIPTION:
#	Create a snapshot from regular filesystem, volume, 
#	or filesystem upon volume, Build a clone file system
#	from the snapshot and verify new files can be written.
#
# STRATEGY:
# 	1. Create snapshot use 3 combination:
#		- Regular filesystem
#		- Regular volume
#		- Filesystem upon volume
# 	2. Clone a new file system from the snapshot
# 	3. Verify the cloned file system is writable
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-08-25)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

# Setup array, 4 elements as a group, refer to:
# i+0: name of a snapshot
# i+1: mountpoint of the snapshot
# i+2: clone created from the snapshot
# i+3: mountpoint of the clone

set -A args "$SNAPFS" "$SNAPDIR" "$TESTPOOL/$TESTCLONE" "$TESTDIR.0" \
	"$SNAPFS1" "$SNAPDIR3" "$TESTPOOL/$TESTCLONE1" "" \
	"$SNAPFS2" "$SNAPDIR2" "$TESTPOOL1/$TESTCLONE2" "$TESTDIR.2"

function setup_all
{
	create_pool $TESTPOOL1 /dev/zvol/$TESTPOOL/$TESTVOL
	log_must $ZFS create $TESTPOOL1/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR2 $TESTPOOL1/$TESTFS

	return 0
}

function cleanup_all
{
	destroy_pool $TESTPOOL1

	[[ -d $TESTDIR2 ]] && \
		log_must $RM -rf $TESTDIR2

	return 0
}

log_assert "Verify a cloned file system is writable."

log_onexit cleanup_all

setup_all

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10

for mtpt in $TESTDIR $TESTDIR2 ; do
	log_note "Populate the $mtpt directory (prior to snapshot)"
	populate_dir $mtpt/before_file $COUNT $NUM_WRITES $BLOCKSZ ITER
done

typeset -i i=0
while (( i < ${#args[*]} )); do 
	#
	# Take a snapshot of the test file system.
	#
	log_must $ZFS snapshot ${args[i]}

	#
	# Clone a new file system from the snapshot
	#
	log_must $ZFS clone ${args[i]} ${args[i+2]}
	if [[ -n ${args[i+3]} ]] ; then
		log_must $ZFS set mountpoint=${args[i+3]} ${args[i+2]}

		FILE_COUNT=`$LS -Al ${args[i+3]} | $GREP -v "total" |  wc -l`
		if [[ $FILE_COUNT -ne $COUNT ]]; then
			$LS -Al ${args[i+3]}
			log_fail "AFTER: ${args[i+3]} contains $FILE_COUNT files(s)."
		fi

		log_note "Verify the ${args[i+3]} directory is writable"
		populate_dir ${args[i+3]}/after_file $COUNT $NUM_WRITES \
			$BLOCKSZ ITER

		FILE_COUNT=`$LS -Al ${args[i+3]}/after* | $GREP -v "total" | wc -l`
		if [[ $FILE_COUNT -ne $COUNT ]]; then
			$LS -Al ${args[i+3]}
			log_fail "${args[i+3]} contains $FILE_COUNT after* files(s)."
		fi
	fi

	(( i = i + 4 ))
done

log_pass "The clone file system is writable."
