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
# Copyright 2014 Spectra Logic Corporation.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib

verify_runnable "global"

function verify_assertion
{
	keyword="$1"
	typeset -i redundancy
	set -A DISKLIST $DISKS

	case "$keyword" in
		"") redundancy=0 ;;
		"mirror") (( redundancy=${#DISKLIST[@]} - 1 )) ;;
		"raidz") redundancy=1 ;;
		"raidz2") redundancy=2 ;;
		"raidz3") redundancy=3 ;;
		*) log_fail "Unknown keyword" ;;
	esac

	echo redundancy is $redundancy

	if [ ${#DISKLIST[@]} -le "$redundancy" ]; then
		log_fail "Insufficiently many disks configured for this test"
	fi

	busy_path $TESTDIR
	# Offline the allowed number of disks
	for ((i=0; i<$redundancy; i=$i+1 )); do
		log_must $ZPOOL offline $TESTPOOL ${DISKLIST[$i]}
	done

	#Verify that offlining any additional disks should fail
	for ((i=$redundancy; i<${#DISKLIST[@]}; i=$i+1 )); do
		log_mustnot $ZPOOL offline $TESTPOOL ${DISKLIST[$i]}
	done
	reap_children

	typeset dir=$(get_device_dir $DISKS)
	verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"
}

log_assert "Turning both disks offline should fail."

for keyword in "" "mirror" "raidz" "raidz2"; do
	child_pids=""
	default_setup_noexit "$keyword $DISKS"
	verify_assertion "$keyword"
	destroy_pool $TESTPOOL
done

log_pass
