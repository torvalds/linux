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
# ident	"@(#)bootfs_004_neg.ksh	1.1	07/05/25 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  bootfs_004_neg
#
# DESCRIPTION:
#
# Invalid pool names are rejected by zpool set bootfs
#
# STRATEGY:
#	1. Try to set bootfs on some non-existent pools
#
#
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-03-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

set -A pools "pool//${TESTCASE_ID}" "pool%d123" "mirror" "c0t0d0s0" "pool*23*" "*po!l" \
	"%s££%^"
typeset VDEV=$TMPDIR/bootfs_004.${TESTCASE_ID}.dat

function cleanup {
	typeset -i=0
	while [ $i -lt "${#pools[@]}" ]; do
		destroy_pool ${pools[$i]}
		i=$(( $i + 1 ))
	done
	$RM $VDEV
}


$ZPOOL set 2>&1 | $GREP bootfs > /dev/null
if [ $? -ne 0 ]
then
        log_unsupported "bootfs pool property not supported on this release."
fi

log_assert "Invalid pool names are rejected by zpool set bootfs"
log_onexit cleanup

# here, we build up a large string and add it to the list of pool names
# a word to the ksh-wary, ${#array[@]} gives you the
# total number of entries in an array, so array[${#array[@]}]
# will index the last entry+1, ksh arrays start at index 0.
COUNT=0
while [ $COUNT -le 1025 ]
do
        bigname="${bigname}o"
        COUNT=$(( $COUNT + 1 ))
done
pools[${#pools[@]}]="$bigname"


create_vdevs $VDEV
typeset -i i=0;

while [ $i -lt "${#pools[@]}" ]
do
	POOL=${pools[$i]}/$FS
	log_mustnot $ZPOOL create $POOL $VDEV
	log_mustnot $ZFS create $POOL/$FS
	log_mustnot $ZPOOL set bootfs=$POOL/$FS $POOL
	
	i=$(( $i + 1 ))
done

log_pass "Invalid pool names are rejected by zpool set bootfs"
