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
# ident	"@(#)zpool_import_missing_003_pos.ksh	1.4	07/10/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_import_missing_003_pos
#
# DESCRIPTION:
# 	Once a pool has been exported, but one or more devices are 
#	overlapped with other exported pool, import should handle
#	this kind of situation properly.
#
# STRATEGY:
#	1. Repeat 1-3, create two test pools upon device files separately.
#	   These two pools should have one or more devices are overlapped.
#	   using the various combinations.
#		- Regular pool
#		- Mirror
#		- Raidz
#	2. Create necessary filesystem and test files.
#	3. Export the test pool.
#	4. Verify 'zpool import -d' with these two pools will have results
#	   as described:
#		- Regular, report error while any number of devices failing.
#		- Mirror could withstand (N-1) devices failing 
#		  before data integrity is compromised 
#		- Raidz could withstand one devices failing 
#		  before data integrity is compromised 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-08-10)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

set -A vdevs "" "mirror" "raidz"

function verify
{
	typeset pool=$1
	typeset fs=$2
	typeset mtpt=$3
	typeset health=$4
	typeset file=$5
	typeset checksum1=$6

	typeset myhealth
	typeset mymtpt
	typeset checksum2

	log_must poolexists $pool

	myhealth=$($ZPOOL list -H -o health $pool)

	[[ $myhealth == $health ]] || \
		log_fail "$pool: Incorrect health ($myhealth), " \
			"expected ($health)." 

	log_must ismounted $pool/$fs

	mymtpt=$(get_prop mountpoint $pool/$fs)
	[[ $mymtpt == $mtpt ]] || \
		log_fail "$pool/$fs: Incorrect mountpoint ($mymtpt), " \
			"expected ($mtpt)."

	[[ ! -e $mtpt/$file ]] && \
		log_fail "$mtpt/$file missing after import."

	checksum2=$($SUM $mymtpt/$file | $AWK '{print $1}')
	[[ "$checksum1" != "$checksum2" ]] && \
		log_fail "Checksums differ ($checksum1 != $checksum2)"

	return 0

}

function cleanup
{
	cd $DEVICE_DIR || log_fail "Unable change directory to $DEVICE_DIR"

	for pool in $TESTPOOL1 $TESTPOOL2; do
		if poolexists "$pool" ; then
			cleanup_filesystem $pool $TESTFS
			destroy_pool $pool
		fi
	done

	[[ -e $DEVICE_ARCHIVE ]] && log_must $TAR xf $DEVICE_ARCHIVE
}

function cleanup_all
{
	cleanup

	# recover dev files
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must create_vdevs ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done

	log_must $RM -f $DEVICE_ARCHIVE
	cd $CWD || log_fail "Unable change directory to $CWD"

}

log_onexit cleanup_all

log_assert "Verify that import could handle device overlapped."

CWD=$PWD

cd $DEVICE_DIR || log_fail "Unable change directory to $DEVICE_DIR"
log_must $TAR cf $DEVICE_ARCHIVE ${DEVICE_FILE}*

checksum1=$($SUM $MYTESTFILE | $AWK '{print $1}')

typeset -i i=0
typeset -i j=0
typeset -i count=0
typeset -i num=0
typeset vdev1=""
typeset vdev2=""
typeset action

while (( num < $GROUP_NUM )); do
	vdev1="$vdev1 ${DEVICE_DIR}/${DEVICE_FILE}$num"
	(( num = num + 1 ))
done

while (( i < ${#vdevs[*]} )); do
	j=0
	while (( j < ${#vdevs[*]} )); do

		(( j != 0 )) && \
			log_must $TAR xf $DEVICE_ARCHIVE

		typeset -i overlap=1
		typeset -i begin
		typeset -i end

		while (( overlap <= $GROUP_NUM )); do
			vdev2=""
			(( begin = $GROUP_NUM - overlap ))
			(( end = 2 * $GROUP_NUM - overlap - 1 ))
			(( num = begin ))
			while (( num <= end )); do
				vdev2="$vdev2 ${DEVICE_DIR}/${DEVICE_FILE}$num"
				(( num = num + 1 ))
			done

			setup_filesystem "$vdev1" $TESTPOOL1 $TESTFS $TESTDIR1 \
				"" ${vdevs[i]}
			log_must $CP $MYTESTFILE $TESTDIR1/$TESTFILE0
			log_must $ZFS umount $TESTDIR1
			poolexists $TESTPOOL1 && \
				log_must $ZPOOL export $TESTPOOL1

			setup_filesystem "$vdev2" $TESTPOOL2 $TESTFS $TESTDIR2 \
				"" ${vdevs[j]}
			log_must $CP $MYTESTFILE $TESTDIR2/$TESTFILE0
			log_must $ZFS umount $TESTDIR2
			poolexists $TESTPOOL2 && \
				log_must $ZPOOL export $TESTPOOL2

			action=log_must
			case "${vdevs[i]}" in
				'mirror') (( overlap == $GROUP_NUM )) && \
					action=log_mustnot
					;;
				'raidz')  (( overlap > 1 )) && \
					action=log_mustnot
               			        ;;
				'')  action=log_mustnot
					;;
			esac

			$action $ZPOOL import -d $DEVICE_DIR $TESTPOOL1
			log_must $ZPOOL import -d $DEVICE_DIR $TESTPOOL2

			if [[ $action == log_must ]]; then
				verify "$TESTPOOL1" "$TESTFS" "$TESTDIR1" \
					"DEGRADED" "$TESTFILE0" "$checksum1"	
			fi
		
			verify "$TESTPOOL2" "$TESTFS" "$TESTDIR2" \
				"ONLINE" "$TESTFILE0" "$checksum1"

			cleanup

			(( overlap = overlap + 1 ))

		done

		((j = j + 1))
	done

	((i = i + 1))
done

log_pass "Import could handle device overlapped."
