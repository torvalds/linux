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
# ident	"@(#)zpool_import_missing_002_pos.ksh	1.4	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib
. $STF_SUITE/tests/cli_root/zpool_import/zpool_import.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_missing_002_pos
#
# DESCRIPTION:
# 	Once a pool has been exported, and one or more devices are 
#	move to other place, import should handle this kind of situation
#	as described:
#		- Regular, report error while any number of devices failing.
#		- Mirror could withstand (N-1) devices failing 
#		  before data integrity is compromised 
#		- Raidz could withstand one devices failing 
#		  before data integrity is compromised 
# 	Verify that is true.
#
# STRATEGY:
#	1. Create test pool upon device files using the various combinations.
#		- Regular pool
#		- Mirror
#		- Raidz
#	2. Create necessary filesystem and test files.
#	3. Export the test pool.
#	4. Move one or more device files to other directory 
#	5. Verify 'zpool import -d' with the new directory 
#	   will handle moved files successfullly.
#	   Using the various combinations.
#		- Regular import
#		- Alternate Root Specified
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

log_onexit cleanup_missing

log_assert "Verify that import could handle moving device."

log_must $MKDIR -p $BACKUP_DEVICE_DIR
cd $DEVICE_DIR || log_fail "Unable change directory to $DEVICE_DIR"

typeset -i i=0
typeset -i count=0
typeset action

function try_import # <action> <poolish> [opts]
{
	typeset action=$1; shift
	typeset poolish="$1"; shift
	log_note "try_import action=$action poolish=$poolish opts='$1'"
	if [ -z "$1" ]; then
		$action $ZPOOL import -d $DEVICE_DIR $poolish
	else
		$action $ZPOOL import -d $DEVICE_DIR $1 $poolish
	fi
	[ "$action" = "log_mustnot" ] && return
	log_must $ZPOOL export $TESTPOOL1
}

while :; do
	typeset vdtype="${vdevs[i]}"

	typeset -i j=0
	while (( j < ${#options[*]} )); do
		typeset opts="${options[j]}"

		[ -n "$vdtype" ] && typestr="$vdtype" || typestr="stripe"
		setup_missing_test_pool $vdtype
		guid=$(get_config $TESTPOOL1 pool_guid $DEVICE_DIR)
		log_note "*** Testing $typestr tvd guid $guid opts '${opts}'"

		typeset -i count=0
		for device in $DEVICE_FILES ; do
			log_mustnot poolexists $TESTPOOL1
			log_must $MV $device $BACKUP_DEVICE_DIR

			(( count = count + 1 ))

			action=log_mustnot
			case "${vdevs[i]}" in
				'mirror') (( count < $GROUP_NUM )) && \
					action=log_must
					;;
				'raidz')  (( count == 1 )) && \
					action=log_must
					;;
 			esac

			log_note "Testing import by name; ${count} moved."
			try_import $action $TESTPOOL1 "$opts"

			log_note "Testing import by GUID; ${count} moved."
			try_import $action $guid "$opts"
		done

		log_must $RM -f $BACKUP_DEVICE_DIR/*
		recreate_missing_files
		((j = j + 1))
	done
	((i = i + 1))
	(( i == ${#vdevs[*]} )) && break
done

log_pass "Import could handle moving device."
