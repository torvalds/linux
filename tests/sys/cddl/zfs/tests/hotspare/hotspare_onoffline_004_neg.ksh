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
# ident	"@(#)hotspare_onoffline_004_neg.ksh	1.5	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_onoffline_004_neg
#
# DESCRIPTION:
#	If a hot spare has been activated,
#	turning that basic vdev offline and back online during I/O completes.
#	Make sure the integrity of the file system and the resilvering. 
#
# STRATEGY:
#	1. Create a storage pool with hot spares
#	2. Activate the hot spare
#	3. Start some random I/O
#	4. Try 'zpool offline' & 'zpool online' with the basic vdev 
#	5. Verify the integrity of the file system and the resilvering.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING STATUS: COMPLETED (2006-06-07)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	kill_all_wp
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	[[ -e $TESTDIR ]] && log_must $RM -rf $TESTDIR/*

	partition_cleanup
}

function kill_all_wp
{
	for wait_pid in $child_pids
	do
		$KILL $wait_pid
		$WAIT $wait_pid
	done
}

function start_all_wp
{
	typeset -i i=0
	typeset -i iters=1

	child_pids=""
	while (( i < iters )); do
		log_note "Invoking $FILE_TRUNC with: $options_display"
		$FILE_TRUNC $options $TESTDIR/$TESTFILE.$i &
		typeset pid=$!

		child_pids="$child_pids $pid"
		((i = i + 1))
	done
}

function verify_assertion # dev
{
	typeset dev=$1
	typeset -i i=0
	typeset -i iters=1
	typeset odev=${pooldevs[0]}

	log_must $ZPOOL replace $TESTPOOL $odev $dev

	i=0
	while (( i < iters )); do
		start_all_wp
		while true; do
			if is_pool_resilvered "$TESTPOOL"; then
				[ -s "$TESTDIR/$TESTFILE.$i" ] && break
			fi
			$SLEEP 2
		done

		kill_all_wp
		log_must test -s $TESTDIR/$TESTFILE.$i

		log_must $ZPOOL offline $TESTPOOL $odev
		log_must check_state $TESTPOOL $odev "offline"

		log_must $ZPOOL online $TESTPOOL $odev
		log_must check_state $TESTPOOL $odev "online"
		(( i = i + 1 ))
	done
		
	log_must $ZPOOL detach $TESTPOOL $dev
}

log_assert "'zpool offline/online <pool> <vdev>' against a spared basic vdev during I/O completes."

log_onexit cleanup

set_devs

options=""
options_display="default options"

[[ -n "$HOLES_FILESIZE" ]] && options=" $options -f $HOLES_FILESIZE "

[[ -n "$HOLES_BLKSIZE" ]] && options="$options -b $HOLES_BLKSIZE "

[[ -n "$HOLES_COUNT" ]] && options="$options -c $HOLES_COUNT "

[[ -n "$HOLES_SEED" ]] && options="$options -s $HOLES_SEED "

[[ -n "$HOLES_FILEOFFSET" ]] && options="$options -o $HOLES_FILEOFFSET "

options="$options -r"

[[ -n "$options" ]] && options_display=$options

typeset child_pid=""

for keyword in "${keywords[@]}" ; do
	setup_hotspares "$keyword"
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL

	iterate_over_hotspares verify_assertion

	verify_filesys "$TESTPOOL" "$TESTPOOL" "$HOTSPARE_TMPDIR"
	destroy_pool "$TESTPOOL"
done

log_pass "'zpool offline/online <pool> <vdev>' against a spared basic vdev during I/O completes."
