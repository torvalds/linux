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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_import_all_001_pos.ksh	1.5	08/11/03 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_all_001_pos
#
# DESCRIPTION:
# Verify that 'zpool import -a' succeeds as root.
#
# STRATEGY:
# 1. Create a group of pools with specified vdev.
# 2. Create zfs filesystems within the given pools.
# 3. Export the pools.
# 4. Verify that import command succeed.
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

set -A options "" "-R $ALTER_ROOT"

typeset -i number=0
typeset -i id=0
typeset -i i=0
typeset checksum1
typeset unwantedpool

function setup_single_disk #disk #pool #fs #mtpt
{
	typeset disk=$1
	typeset pool=$2
	typeset fs=${3##/}
	typeset mtpt=$4

	setup_filesystem "$disk" "$pool" "$fs" "$mtpt"

	log_must $CP $MYTESTFILE $mtpt/$TESTFILE0

	log_must $ZPOOL export $pool

	[[ -d $mtpt ]] && \
		$RM -rf $mtpt
}

function cleanup_all
{
	typeset -i id=0

	#
	# Try import individually if 'import -a' failed.
	# 
	for pool in `$ZPOOL import | $GREP "pool:" | $AWK '{print $2}'`; do
		$ZPOOL import -f $pool
	done

	for pool in `$ZPOOL import -d $DEVICE_DIR | $GREP "pool:" | $AWK '{print $2}'`; do
		log_must $ZPOOL import -d $DEVICE_DIR -f $pool
	done

	while (( id < number )); do
		if ! poolexists ${TESTPOOL}-$id ; then
			(( id = id + 1 ))
			continue
		fi

		if (( id == 0 )); then
			log_must $ZPOOL export ${TESTPOOL}-$id

			[[ -d /${TESTPOOL}-$id ]] && \
				log_must $RM -rf /${TESTPOOL}-$id

			log_must $ZPOOL import -f ${TESTPOOL}-$id $TESTPOOL

			[[ -e $TESTDIR/$TESTFILE0 ]] && \
				log_must $RM -rf $TESTDIR/$TESTFILE0
		else
			cleanup_filesystem "${TESTPOOL}-$id" $TESTFS

			destroy_pool ${TESTPOOL}-$id
		fi

		(( id = id + 1 ))
        done

	[[ -d $ALTER_ROOT ]] && \
		$RM -rf $ALTER_ROOT
}

function checksum_all #alter_root 
{
	typeset alter_root=$1
	typeset -i id=0
	typeset file
	typeset checksum2

	while (( id < number )); do
		if (( id == 2 )); then
			(( id = id + 1 ))
			continue
		fi

		if (( id == 0 )); then
			file=${alter_root}/$TESTDIR/$TESTFILE0
		else
			file=${alter_root}/$TESTDIR.$id/$TESTFILE0
		fi	
		[[ ! -e $file ]] && \
			log_fail "$file missing after import."

		checksum2=$($SUM $file | $AWK '{print $1}')
		[[ "$checksum1" != "$checksum2" ]] && \
			log_fail "Checksums differ ($checksum1 != $checksum2)"

		(( id = id + 1 ))
	done

	return 0
}


log_assert "Verify that 'zpool import -a' succeeds as root."

log_onexit cleanup_all

checksum1=$($SUM $MYTESTFILE | $AWK '{print $1}')

log_must $ZPOOL export $TESTPOOL
log_must $ZPOOL import $TESTPOOL ${TESTPOOL}-0
log_must $CP $MYTESTFILE $TESTDIR/$TESTFILE0
log_must $ZPOOL export ${TESTPOOL}-0
[[ -d /${TESTPOOL}-0 ]] && \
	log_must $RM -rf /${TESTPOOL}-0

#
# setup exported pools on normal devices
#
number=1
while (( number <= $GROUP_NUM )); do
	if [[ `$UNAME -s` != "FreeBSD" ]]; then
		if (( number == 2)); then
			(( number = number + 1 ))
			continue
		fi
	fi
	set_partition $number "" $PART_SIZE ${ZFS_DISK2}

	setup_single_disk "${ZFS_DISK2}p${number}" \
		"${TESTPOOL}-$number" \
		"$TESTFS" \
		"$TESTDIR.$number"

	(( number = number + 1 ))
done

#
# setup exported pools on raw files
#
for disk in $DEVICE_FILES
do

	setup_single_disk "$disk" \
		"${TESTPOOL}-$number" \
		"$TESTFS" \
		"$TESTDIR.$number"

	(( number = number + 1 ))
done

while (( i < ${#options[*]} )); do
	
	log_must $ZPOOL import -d /dev -d $DEVICE_DIR ${options[i]} -a -f

	# destroy unintentional imported pools
	typeset exclude=`eval $ECHO \"'(${KEEP})'\"`
	for unwantedpool in $($ZPOOL list -H -o name \
	     | $EGREP -v "$exclude" | $GREP -v $TESTPOOL); do
		log_must $ZPOOL export $unwantedpool
	done
	
	if [[ -n ${options[i]} ]]; then
		checksum_all $ALTER_ROOT
	else
		checksum_all
	fi

	id=0 
	while (( id < number )); do
		if poolexists ${TESTPOOL}-$id ; then
			log_must $ZPOOL export ${TESTPOOL}-$id
		fi
		(( id = id + 1 ))
	done

	(( i = i + 1 ))
done

log_pass "'zpool import -a' succeeds as root."
