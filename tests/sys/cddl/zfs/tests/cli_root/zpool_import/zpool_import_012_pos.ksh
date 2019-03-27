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
# ident	"@(#)zpool_import_012_pos.ksh	1.4	09/05/19 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_012_pos
#
# DESCRIPTION:
# Once a pool has been exported, it should be recreated after a
# successful import, all the sub-filesystems within it should all be restored,
# include mount & share status. Verify that is true.
#
# STRATEGY:
#	1. Create the test pool and hierarchical filesystems.
#	2. Export the test pool, or destroy the test pool,
#		depend on testing import [-Df].
#	3. Import it using the various combinations.
#		- Regular import
#		- Alternate Root Specified
#	4. Verify the mount & share status is restored. 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-11-01)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

set -A pools "$TESTPOOL" "$TESTPOOL1"
set -A devs "" "-d $DEVICE_DIR"
set -A options "" "-R $ALTER_ROOT"
set -A mtpts "$TESTDIR" "$TESTDIR1"


function cleanup
{
	typeset -i i=0

	while (( i < ${#pools[*]} )); do
		if poolexists ${pools[i]} ; then
			log_must $ZPOOL export ${pools[i]}
			log_note "Try to import ${devs[i]} ${pools[i]}"
			$ZPOOL import ${devs[i]} ${pools[i]}
		else
			log_note "Try to import $option ${devs[i]} ${pools[i]}"
			$ZPOOL import $option ${devs[i]} ${pools[i]}
		fi

		if poolexists ${pools[i]} ; then
			is_shared ${pools[i]} && \
				log_must $ZFS set sharenfs=off ${pools[i]}

			ismounted "${pools[i]}/$TESTFS" || \
				log_must $ZFS mount ${pools[i]}/$TESTFS
		fi
	
		((i = i + 1))
	done

	destroy_pool $TESTPOOL1

	if datasetexists $TESTPOOL/$TESTFS ; then
		log_must $ZFS destroy -Rf $TESTPOOL/$TESTFS
	fi
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	[[ -d $ALTER_ROOT ]] && \
		log_must $RM -rf $ALTER_ROOT
}

log_onexit cleanup

log_assert "Verify all mount & share status of sub-filesystems within a pool \
	can be restored after import [-Df]."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1
for pool in ${pools[@]} ; do
	log_must $ZFS create $pool/$TESTFS/$TESTCTR
	log_must $ZFS create $pool/$TESTFS/$TESTCTR/$TESTCTR1
	log_must $ZFS set canmount=off $pool/$TESTFS/$TESTCTR
	log_must $ZFS set canmount=off $pool/$TESTFS/$TESTCTR/$TESTCTR1
	log_must $ZFS create $pool/$TESTFS/$TESTCTR/$TESTFS1
	log_must $ZFS create $pool/$TESTFS/$TESTCTR/$TESTCTR1/$TESTFS1
	log_must $ZFS create $pool/$TESTFS/$TESTFS1
	log_must $ZFS snapshot $pool/$TESTFS/$TESTFS1@snap
	log_must $ZFS clone $pool/$TESTFS/$TESTFS1@snap $pool/$TESTCLONE1
done

typeset mount_fs="$TESTFS $TESTFS/$TESTFS1 $TESTCLONE1 \
		$TESTFS/$TESTCTR/$TESTFS1 $TESTFS/$TESTCTR/$TESTCTR1/$TESTFS1" 
typeset nomount_fs="$TESTFS/$TESTCTR $TESTFS/$TESTCTR/$TESTCTR1"

typeset -i i=0
typeset -i j=0
typeset basedir

for option in "" "-Df" ; do
	i=0
	while (( i < ${#pools[*]} )); do
		pool=${pools[i]}
		guid=$(get_config $pool pool_guid)
		j=0
		while (( j < ${#options[*]} )); do
			typeset f_share=""
			if ((RANDOM % 2 == 0)); then
				log_note "Set sharenfs=on $pool"
				log_must $ZFS set sharenfs=on $pool
				log_must is_shared $pool
				f_share="true"
			fi

			if [[ -z $option ]]; then
				log_must $ZPOOL export $pool
			else
				log_must $ZPOOL destroy $pool
			fi

			typeset target=$pool
			if (( RANDOM % 2 == 0 )) ; then
				log_note "Import by guid."
				if [[ -z $guid ]]; then
					log_fail "guid should not be empty!"
				else
					target=$guid
				fi
			fi
			log_must $ZPOOL import $option \
				${devs[i]} ${options[j]} $target

			log_must poolexists $pool

			for fs in $mount_fs ; do
				log_must ismounted $pool/$fs
				[[ -n $f_share ]] && \
					log_must is_shared $pool/$fs
			done

			for fs in $nomount_fs ; do
				log_mustnot ismounted $pool/$fs
				log_mustnot is_shared $pool/$fs
			done

			if [[ -n $f_share ]] ; then
				log_must $ZFS set sharenfs=off $pool
				log_mustnot is_shared $pool
			fi

			((j = j + 1))
		done

		((i = i + 1))
	done
done

log_pass "All mount & share status of sub-filesystems within a pool \
	can be restored after import [-Df]."
