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
# ident	"@(#)zfs_mount_all_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

#################################################################################
# __stc_assertion_start
#
# ID: zfs_mount_all_001_pos
#
# DESCRIPTION:
#       Verify that 'zfs mount -a' succeeds as root.
#
# STRATEGY:
#       1. Create a group of pools with specified vdev.
#       2. Create zfs filesystems within the given pools.
#       3. Unmount all the filesystems.
#       4. Verify that 'zfs mount -a' command succeed,
#	   and all available ZFS filesystems are mounted.
#	5. Verify that 'zfs mount' is identical with 'df -F zfs'	
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

set -A fs "$TESTFS" "$TESTFS1"
set -A ctr "" "$TESTCTR" "$TESTCTR/$TESTCTR1" "$TESTCTR1"
set -A vol "$TESTVOL" "$TESTVOL1"

function setup_all
{
	typeset -i i=0
	typeset -i j=0
	typeset path

	while (( i < ${#ctr[*]} )); do

		path=${TEST_BASE_DIR%%/}/testroot${TESTCASE_ID}/$TESTPOOL
		if [[ -n ${ctr[i]} ]]; then
			path=$path/${ctr[i]}

			setup_filesystem "$DISKS" "$TESTPOOL" \
				"${ctr[i]}" "$path" \
				"ctr"
		fi

		if is_global_zone ; then
			j=0
			while (( j < ${#vol[*]} )); do
				setup_filesystem "$DISKS" "$TESTPOOL" \
					"${ctr[i]}/${vol[j]}" \
					"$path/${vol[j]}" \
					"vol"
				((j = j + 1))
			done
		fi

		j=0
		while (( j < ${#fs[*]} )); do
			setup_filesystem "$DISKS" "$TESTPOOL" \
				"${ctr[i]}/${fs[j]}" \
				"$path/${fs[j]}"
			((j = j + 1))
		done

		((i = i + 1))
	done

	return 0
}

function cleanup_all
{
	typeset -i i=0
	typeset -i j=0
	typeset path

	((i = ${#ctr[*]} - 1))

	while (( i >= 0 )); do
		if is_global_zone ; then
			j=0
			while (( j < ${#vol[*]} )); do
				cleanup_filesystem "$TESTPOOL" \
					"${ctr[i]}/${vol[j]}"
				((j = j + 1))
			done
		fi

		j=0
		while (( j < ${#fs[*]} )); do
			cleanup_filesystem "$TESTPOOL" \
				"${ctr[i]}/${fs[j]}"
			((j = j + 1))
		done

		[[ -n ${ctr[i]} ]] && \
			cleanup_filesystem "$TESTPOOL" "${ctr[i]}"

		((i = i - 1))
	done

	[[ -d ${TEST_BASE_DIR%%/}/testroot${TESTCASE_ID} ]] && \
		$RM -rf ${TEST_BASE_DIR%%/}/testroot${TESTCASE_ID}
}

function verify_all
{
	typeset -i i=0
	typeset -i j=0
	typeset path

	while (( i < ${#ctr[*]} )); do

		path=$TESTPOOL
		[[ -n ${ctr[i]} ]] && \
			path=$path/${ctr[i]}

		if is_global_zone ; then
			j=0
			while (( j < ${#vol[*]} )); do
				log_mustnot mounted "$path/${vol[j]}"
				((j = j + 1))
			done
		fi

		j=0
		while (( j < ${#fs[*]} )); do
			log_must mounted "$path/${fs[j]}"
			((j = j + 1))
		done

		log_must mounted "$path"

		((i = i + 1))
	done

	return 0
}


log_assert "Verify that 'zfs $mountall' succeeds as root, " \
	"and all available ZFS filesystems are mounted."

log_onexit cleanup_all

log_must setup_all

log_must $ZFS $unmountall

log_must $ZFS $mountall

verify_all

log_note "Verify that 'zfs $mountcmd' will display " \
	"all ZFS filesystems currently mounted."

verify_mount_display

log_pass "'zfs $mountall' succeeds as root, " \
	"and all available ZFS filesystems are mounted."
