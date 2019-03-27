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
# ident	"@(#)zfs_unmount_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unmount_001_pos
#
# DESCRIPTION:
# Creates a file system and verifies that it can be unmounted
# using each of the various unmount options and sub-command
# variants.
#
# STRATEGY:
# 1. Create and mount a file system as necessary.
# 2. Umount the file system using the various combinations.
# 	- With force option.
# 	- Without force option.
# 	- Using the unmount sub-command.
# 	- Using the umount sub-command.
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
	mounted $TESTDIR2 && \
		log_must $ZFS umount -f $TESTDIR2

	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must $ZFS destroy $TESTPOOL/$TESTFS2

	[[ -d $TESTDIR2 ]] && \
		log_must $RM -rf $TESTDIR2
}
function do_unmount
{
	typeset cmd=$1
	typeset opt=$2
	typeset mnt=$3

	[[ ! -d $TESTDIR2 ]] && \
	    log_must $MKDIR $TESTDIR2

	if ! datasetexists $TESTPOOL/$TESTFS2 ; then
		log_must $ZFS create $TESTPOOL/$TESTFS2
		log_must $ZFS set mountpoint=$TESTDIR2 \
		    $TESTPOOL/$TESTFS2
	fi

	unmounted $TESTPOOL/$TESTFS2 && \
		log_must $ZFS mount $TESTPOOL/$TESTFS2 

	log_must $ZFS $cmd $options $mnt

	unmounted "$mnt" || \
		log_fail "Unable to unmount $options $mnt"

	log_note "Successfully unmounted $options $mnt"
}

log_onexit cleanup

set -A cmd "umount" "unmount"
set -A options "" "-f"
set -A dev "$TESTPOOL/$TESTFS2" "$TESTDIR2"

log_assert "Verify the u[n]mount [-f] sub-command."

typeset -i i=0
typeset -i j=0
typeset -i k=0
while [[ $i -lt ${#cmd[*]} ]]; do
	j=0
	while [[ $j -lt ${#options[*]} ]]; do
		k=0
		while [[ $k -lt ${#dev[*]} ]]; do
			do_unmount "${cmd[i]}" "${options[j]}" \
			    "${dev[k]}"

			((k = k + 1))
		done

		((j = j + 1))
	done

	((i = i + 1))
done

log_pass "zfs u[n]mount [-f] completed successfully."
