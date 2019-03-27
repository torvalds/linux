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
# ident	"@(#)zpool_import_rename_001_pos.ksh	1.3	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_rename_001_pos
#
# DESCRIPTION:
# An exported pool can be imported under a different name. Hence
# we test that a previously exported pool can be renamed.
#
# STRATEGY:
#	1. Copy a file into the default test directory.
#	2. Umount the default directory.
#	3. Export the pool.
#	4. Import the pool using the name ${TESTPOOL}-new,
#	   and using the various combinations.
#               - Regular import
#               - Alternate Root Specified
#	5. Verify it exists in the 'zpool list' output.
#	6. Verify the default file system is mounted and that the file
#	   from step (1) is present.
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

verify_runnable "global"

set -A pools "$TESTPOOL" "$TESTPOOL1"
set -A devs "" "-d $DEVICE_DIR"
set -A options "" "-R $ALTER_ROOT"
set -A mtpts "$TESTDIR" "$TESTDIR1"


function cleanup
{
	typeset -i i=0
	while (( i < ${#pools[*]} )); do
		if poolexists "${pools[i]}-new" ; then
			log_must $ZPOOL export "${pools[i]}-new"

			[[ -d /${pools[i]}-new ]] && \
				log_must $RM -rf /${pools[i]}-new
 
			log_must $ZPOOL import ${devs[i]} \
				"${pools[i]}-new" ${pools[i]}
		fi

		datasetexists "${pools[i]}" || \
			log_must $ZPOOL import ${devs[i]} ${pools[i]}

		ismounted "${pools[i]}/$TESTFS" || \
			log_must $ZFS mount ${pools[i]}/$TESTFS
	
		[[ -e ${mtpts[i]}/$TESTFILE0 ]] && \
			log_must $RM -rf ${mtpts[i]}/$TESTFILE0

		((i = i + 1))

	done

	cleanup_filesystem $TESTPOOL1 $TESTFS $TESTDIR1

	destroy_pool $TESTPOOL1

	[[ -d $ALTER_ROOT ]] && \
		log_must $RM -rf $ALTER_ROOT
}

function perform_inner_test
{
	target=$1

	log_must $ZPOOL import ${devs[i]} ${options[j]} \
		$target ${pools[i]}-new

	log_must poolexists "${pools[i]}-new"

	log_must ismounted ${pools[i]}-new/$TESTFS

	basedir=${mtpts[i]}
	[[ -n ${options[j]} ]] && \
		basedir=$ALTER_ROOT/${mtpts[i]}

	[[ ! -e $basedir/$TESTFILE0 ]] && \
		log_fail "$basedir/$TESTFILE0 missing after import."

	checksum2=$($SUM $basedir/$TESTFILE0 | $AWK '{print $1}')
	[[ "$checksum1" != "$checksum2" ]] && \
		log_fail "Checksums differ ($checksum1 != $checksum2)"

	log_must $ZPOOL export "${pools[i]}-new"

	[[ -d /${pools[i]}-new ]] && \
		log_must $RM -rf /${pools[i]}-new

	target=${pools[i]}-new
	if (( RANDOM % 2 == 0 )) ; then
		target=$guid
	fi
	log_must $ZPOOL import ${devs[i]} $target ${pools[i]}
}

log_onexit cleanup

log_assert "Verify that an imported pool can be renamed."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1
checksum1=$($SUM $MYTESTFILE | $AWK '{print $1}')
 
typeset -i i=0
typeset -i j=0
typeset basedir

while (( i < ${#pools[*]} )); do
	guid=$(get_config ${pools[i]} pool_guid)
	log_must $CP $MYTESTFILE ${mtpts[i]}/$TESTFILE0

	log_must $ZFS umount ${mtpts[i]}

	j=0
	while (( j <  ${#options[*]} )); do
		log_must $ZPOOL export ${pools[i]}

		[[ -d /${pools[i]} ]] && \
			log_must $RM -rf /${pools[i]}

		log_note "Testing import by name."
		perform_inner_test ${pools[i]}

		log_must $ZPOOL export ${pools[i]}

		log_note "Testing import by GUID."
		perform_inner_test $guid

		((j = j + 1))
	done

	((i = i + 1))
done

log_pass "Successfully imported and renamed a ZPOOL"
