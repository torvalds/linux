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
# ident	"@(#)mountpoint_002_pos.ksh	1.1	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: mountpoint_002_pos
#
# DESCRIPTION:
# 	If ZFS is currently managing the file system but it is currently unmoutned, 
#	and the mountpoint property is changed, the file system remains unmounted.
#
# STRATEGY:
# 1. Setup a pool and create fs, ctr within it.
# 2. Unmount that dataset
# 2. Change the mountpoint to the valid mountpoint value.
# 3. Check the file system remains unmounted.
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

set -A dataset "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR"

set -A values "$TESTDIR2" "$TESTDIR_NOTEXISTING"

function cleanup
{
	log_must $ZFS set mountpoint=$old_ctr_mpt $TESTPOOL/$TESTCTR
	log_must $ZFS set mountpoint=$old_fs_mpt $TESTPOOL/$TESTFS
	log_must $ZFS mount -a
	[[ -d $TESTDIR2 ]] && log_must $RM -r $TESTDIR2
	[[ -d $TESTDIR_NOTEXISTING ]] && log_must $RM -r $TESTDIR_NOTEXISTING
}

log_assert "Setting a valid mountpoint for an unmounted file system, \
	it remains unmounted."
log_onexit cleanup

old_fs_mpt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
[[ $? != 0 ]] && \
	log_fail "Unable to get the mountpoint property for $TESTPOOL/$TESTFS"
old_ctr_mpt=$(get_prop mountpoint $TESTPOOL/$TESTCTR)
[[ $? != 0 ]] && \
	log_fail "Unable to get the mountpoint property for $TESTPOOL/$TESTCTR"

if [[ ! -d $TESTDIR2 ]]; then
	log_must $MKDIR $TESTDIR2
fi

typeset -i i=0
typeset -i j=0
while (( i < ${#dataset[@]} )); do
	j=0
	if ismounted ${dataset[i]} ; then
		log_must $ZFS unmount ${dataset[i]}
	fi
	log_mustnot ismounted ${dataset[i]}
	while (( j < ${#values[@]} )); do
		set_n_check_prop "${values[j]}" "mountpoint" \
			"${dataset[i]}"
		log_mustnot ismounted ${dataset[i]}
		(( j += 1 ))
	done
	cleanup
	(( i += 1 ))
done

log_pass "Setting a valid mountpoint for an unmounted file system, \
	it remains unmounted."
