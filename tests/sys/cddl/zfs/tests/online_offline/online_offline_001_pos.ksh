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
	busy_path $TESTDIR
	for disk in $DISKS; do
		log_must $ZPOOL offline $TESTPOOL $disk
		check_state $TESTPOOL $disk "offline"
		if [[ $? != 0 ]]; then
			log_fail "$disk of $TESTPOOL is not offline."
		fi

		log_must $ZPOOL online $TESTPOOL $disk
		check_state $TESTPOOL $disk "online"
		if [[ $? != 0 ]]; then
			log_fail "$disk of $TESTPOOL did not match online state"
		fi
	done
	reap_children

	typeset dir=$(get_device_dir $DISKS)
	verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"
}

log_assert "Turning a disk offline and back online during I/O completes."
log_onexit cleanup

for keyword in "mirror" "raidz"; do
	typeset child_pid=""
	default_setup_noexit "$keyword $DISKS"
	verify_assertion
	destroy_pool $TESTPOOL
done

log_pass
