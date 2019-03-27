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
# ident	"@(#)zpool_import_002_pos.ksh	1.3	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_002_pos
#
# DESCRIPTION:
# Verify that an exported pool cannot be imported
# more than once.
#
# STRATEGY:
#	1. Populate the default test directory and unmount it.
#	2. Export the default test pool.
#	3. Import it using the various combinations.
#		- Regular import
#		- Alternate Root Specified
#	4. Verify it shows up under 'zpool list'.
#	5. Verify it contains a file.
#	6. Attempt to import it for a second time. Verify this fails.
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
		poolexists ${pools[i]} && \
			log_must $ZPOOL export ${pools[i]}

		datasetexists "${pools[i]}/$TESTFS" || \
			log_must $ZPOOL import ${devs[i]} ${pools[i]}

		ismounted "${pools[i]}/$TESTFS" || \
			log_must $ZFS mount ${pools[i]}/$TESTFS
	
		[[ -e ${mtpts[i]}/$TESTFILE0 ]] && \
			log_must $RM -rf ${mtpts[i]}/$TESTFILE0

		((i = i + 1))
	done

	cleanup_filesystem $TESTPOOL1 $TESTFS

	destroy_pool $TESTPOOL1

	[[ -d $ALTER_ROOT ]] && \
		log_must $RM -rf $ALTER_ROOT
}

log_onexit cleanup

log_assert "Verify that an exported pool cannot be imported more than once."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1

checksum1=$($SUM $MYTESTFILE | $AWK '{print $1}')

typeset -i i=0
typeset -i j=0
typeset basedir

function inner_test
{
	typeset pool=$1
	typeset target=$2
	typeset devs=$3
	typeset opts=$4
	typeset mtpt=$5

	log_must $ZPOOL import ${devs} ${opts} $target
	log_must poolexists $pool
	log_must ismounted $pool/$TESTFS

	basedir=$mtpt
	[ -n "$opts" ] && basedir="$ALTER_ROOT/$mtpt"

	[ ! -e "$basedir/$TESTFILE0" ] && \
		log_fail "ERROR: $basedir/$TESTFILE0 missing after import."

	checksum2=$($SUM $basedir/$TESTFILE0 | $AWK '{print $1}')
	[[ "$checksum1" != "$checksum2" ]] && \
		log_fail "ERROR: Checksums differ ($checksum1 != $checksum2)"

	log_mustnot $ZPOOL import $devs $target
}

while (( i < ${#pools[*]} )); do
	log_must $CP $MYTESTFILE ${mtpts[i]}/$TESTFILE0

	log_must $ZFS umount ${mtpts[i]}

	j=0
	while (( j <  ${#options[*]} )); do
		typeset pool=${pools[i]}
		typeset vdevdir=""

		log_must $ZPOOL export $pool

		[ "$pool" = "$TESTPOOL1" ] && vdevdir="$DEVICE_DIR"
		guid=$(get_config $pool pool_guid $vdevdir)
		log_must test -n "$guid"
		log_note "Importing '$pool' by guid '$guid'"
		inner_test $pool $guid "${devs[i]}" "${options[j]}" ${mtpts[i]}

		log_must $ZPOOL export $pool

		log_note "Importing '$pool' by name."
		inner_test $pool $pool "${devs[i]}" "${options[j]}" ${mtpts[i]}

		((j = j + 1))
	done

	((i = i + 1))

done

log_pass "Able to import exported pools and import only once."
