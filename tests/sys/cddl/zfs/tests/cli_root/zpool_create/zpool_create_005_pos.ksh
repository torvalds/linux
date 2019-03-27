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
# ident	"@(#)zpool_create_005_pos.ksh	1.3	08/02/27 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_005_pos
#
# DESCRIPTION:
# 'zpool create [-R root][-m mountpoint] <pool> <vdev> ...' can create an
#  alternate root pool or a new pool mounted at the specified mountpoint.
#
# STRATEGY:
# 1. Create a pool with '-m' option 
# 2. Verify the pool is mounted at the specified mountpoint
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		log_must $ZPOOL destroy -f $TESTPOOL

	for dir in $TMPDIR/zpool_create_005_pos $TESTDIR1; do
		[[ -d $dir ]] && $RM -rf $dir
	done
}

log_assert "'zpool create [-R root][-m mountpoint] <pool> <vdev> ...' can create" \
	"an alternate pool or a new pool mounted at the specified mountpoint."
log_onexit cleanup

set -A pooltype "" "mirror" "raidz" "raidz1" "raidz2"

#prepare raw file for file disk
TDIR=$TMPDIR/zpool_create_005_pos
FBASE=$TDIR/file
log_must $MKDIR $TDIR
log_must create_vdevs $FBASE.0 $FBASE.1 $FBASE.2 $FBASE.3
#Remove the directory with name as pool name if it exists
[[ -d /$TESTPOOL ]] && $RM -rf /$TESTPOOL

for opt in "-R $TESTDIR1" "-m $TESTDIR1" \
	"-R $TESTDIR1 -m $TESTDIR1" "-m $TESTDIR1 -R $TESTDIR1"
do
	i=0
	while (( i < ${#pooltype[*]} )); do 
		#Remove the testing pool and its mount directory 
		poolexists $TESTPOOL && \
			log_must $ZPOOL destroy -f $TESTPOOL
		[[ -d $TESTDIR1 ]] && $RM -rf $TESTDIR1
		log_must $ZPOOL create $opt $TESTPOOL ${pooltype[i]} \
			$FBASE.1 $FBASE.2 $FBASE.3
		mpt=`$ZFS mount | $EGREP "^$TESTPOOL[^/]" | $AWK '{print $2}'`
		(( ${#mpt} == 0 )) && \
			log_fail "$TESTPOOL created with $opt is not mounted."
		mpt_val=$(get_prop "mountpoint" $TESTPOOL)
		[[ "$mpt" != "$mpt_val" ]] && \
			log_fail "The value of mountpoint property is different\
				from the output of zfs mount"
		if [[ "$opt" == "-R $TESTDIR1" ]]; then
			expected_mpt=${TESTDIR1}/${TESTPOOL}
		elif [[ "$opt" == "-m $TESTDIR1" ]]; then
			expected_mpt=${TESTDIR1}
		else
			expected_mpt=${TESTDIR1}${TESTDIR1}
		fi
		[[ ! -d $expected_mpt ]] && \
			log_fail "$expected_mpt is not created auotmatically."
		[[ "$mpt" != "$expected_mpt" ]] && \
			log_fail "$expected_mpt is mounted on ${mpt} instead of $expected_mpt."

		[[ -d /$TESTPOOL ]] && \
			log_fail "The default mountpoint /$TESTPOOL is created" \
				"while with $opt option."

		(( i = i + 1 ))
	done
done

log_pass "'zpool create [-R root][-m mountpoint] <pool> <vdev> ...' works as expected."
