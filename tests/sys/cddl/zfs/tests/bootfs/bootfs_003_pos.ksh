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
# ident	"@(#)bootfs_003_pos.ksh	1.2	08/11/03 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  bootfs_003_pos
#
# DESCRIPTION:
#
# Valid pool names are accepted
#
# STRATEGY:
# 1. Using a list of valid pool names
# 2. Create a filesystem in that pool
# 2. Verify we can set the bootfs to that filesystem
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

set -A pools "pool.${TESTCASE_ID}" "pool123" "mypool"
typeset VDEV=$TMPDIR/bootfs_003.${TESTCASE_ID}.dat

function cleanup {
	typeset -i i=0
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

log_onexit cleanup

log_assert "Valid pool names are accepted by zpool set bootfs"
create_vdevs $VDEV

typeset -i i=0;

while [ $i -lt "${#pools[@]}" ]
do
	POOL=${pools[$i]}
	log_must $ZPOOL create $POOL $VDEV
	log_must $ZFS create $POOL/$FS

	enc=$(get_prop encryption $POOL/$FS)
	if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
		log_unsupported "bootfs pool property not supported \
when encryption is set to on."
	fi

	log_must $ZPOOL set bootfs=$POOL/$FS $POOL
	RES=$($ZPOOL get bootfs $POOL | $TAIL -1 | $AWK '{print $3}' )
	if [ $RES != "$POOL/$FS" ]
	then
		log_fail "Expected $RES == $POOL/$FS"
	fi
	log_must $ZPOOL destroy -f $POOL
	i=$(( $i + 1 ))
done

log_pass "Valid pool names are accepted by zpool set bootfs"
