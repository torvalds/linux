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
# ident	"@(#)zpool_import_missing_001_pos.ksh	1.4	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib
. $STF_SUITE/tests/cli_root/zpool_import/zpool_import.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_missing_001_pos
#
# DESCRIPTION:
# 	Once a pool has been exported, and one or more devices are 
#	damaged or missing (d/m), import should handle this kind of situation
#	as described:
#		- Regular, report error while any number of devices failing.
#		- Mirror could withstand (N-1) devices failing 
#		  before data integrity is compromised 
#		- Raidz could withstand one devices failing 
#		  before data integrity is compromised 
# 	Verify those are true.
#
# STRATEGY:
#	1. Create test pool upon device files using the various combinations.
#		- Regular pool
#		- Mirror
#		- Raidz
#	2. Create necessary filesystem and test files.
#	3. Export the test pool.
#	4. Remove one or more devices
#	5. Verify 'zpool import' will handle d/m device successfully.
#	   Using the various combinations.
#		- Regular import
#		- Alternate Root Specified
#	   It should be succeed with single d/m device upon 'raidz' & 'mirror',
#	   but failed against 'regular' or more d/m devices.
#	6. If import succeed, verify following is true:
#		- The pool shows up under 'zpool list'.
#		- The pool's health should be DEGRADED.
#		- It contains the correct test file
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-08-01)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

set -A vdevs "" "mirror" "raidz"
set -A options "" "-R $ALTER_ROOT"

function perform_inner_test
{
	typeset action=$1
	typeset import_opts=$2
	typeset target=$3
	typeset basedir

	$action $ZPOOL import -d $DEVICE_DIR ${import_opts} $target
	[[ $action == "log_mustnot" ]] && return

	log_must poolexists $TESTPOOL1

	health=$($ZPOOL list -H -o health $TESTPOOL1)
	[[ "$health" == "DEGRADED" ]] || \
		log_fail "ERROR: $TESTPOOL1: Incorrect health '$health'"
	log_must ismounted $TESTPOOL1/$TESTFS

	basedir=$TESTDIR1
	[[ -n "${import_opts}" ]] && basedir=$ALTER_ROOT/$TESTDIR1
	[[ ! -e "$basedir/$TESTFILE0" ]] && \
		log_fail "ERROR: $basedir/$TESTFILE0 missing after import."

	checksum2=$($SUM $basedir/$TESTFILE0 | $AWK '{print $1}')
	[[ "$checksum1" != "$checksum2" ]] && \
		log_fail "ERROR: Checksums differ ($checksum1 != $checksum2)"

	log_must $ZPOOL export $TESTPOOL1
}

log_onexit cleanup_missing

log_assert "Verify that import could handle damaged or missing device."

CWD=$PWD
cd $DEVICE_DIR || log_fail "ERROR: Unable change directory to $DEVICE_DIR"

checksum1=$($SUM $MYTESTFILE | $AWK '{print $1}')

typeset -i i=0
while :; do
	typeset vdtype="${vdevs[i]}"

	typeset -i j=0
	while (( j < ${#options[*]} )); do
		typeset opts="${options[j]}"
		[ -n "$vdtype" ] && typestr="$vdtype" || typestr="stripe"

		# Prepare the pool.
		setup_missing_test_pool $vdtype
		guid=$(get_config $TESTPOOL1 pool_guid $DEVICE_DIR)
		log_note "*** Testing $typestr tvd guid $guid opts '${opts}'"

		typeset -i count=0
		for device in $DEVICE_FILES ; do
			log_mustnot poolexists $TESTPOOL1
			log_must $RM -f $device

			(( count = count + 1 ))

			action=log_must
			case "$vdtype" in
				'mirror') (( count == $GROUP_NUM )) && \
						action=log_mustnot
					;;
				'raidz')  (( count > 1 )) && \
						action=log_mustnot
					;;
				'')  action=log_mustnot
					;;
			esac

			log_note "Testing import by name; ${count} removed."
			perform_inner_test $action "${opts}" $TESTPOOL1

			log_note "Testing import by GUID; ${count} removed."
			perform_inner_test $action "${opts}" $guid
		done

		recreate_missing_files
		(( j = j + 1 ))
	done
	(( i = i + 1 ))
	(( i == ${#vdevs[*]} )) && break
done

log_pass "Import could handle damaged or missing device."
