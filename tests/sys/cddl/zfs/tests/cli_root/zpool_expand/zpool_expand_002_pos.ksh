#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)zpool_expand_002_pos.ksh	1.1	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zpool_expand_002_pos
#
# DESCRIPTION:
# After zpool online -e poolname zvol vdevs, zpool can autoexpand by 
# Dynamic LUN Expansion
# 
#
# STRATEGY:
# 1) Create a pool
# 2) Create volume on top of the pool
# 3) Create pool by using the zvols
# 4) Expand the vol size by zfs set volsize
# 5  Use zpool online -e to online the zvol vdevs
# 6) Check that the pool size was expaned
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-06-12)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1

	for i in 1 2 3; do
		if datasetexists $VFS/vol$i; then
			log_must $ZFS destroy $VFS/vol$i
		fi
	done
}

log_onexit cleanup

log_assert "zpool can expand after zpool online -e zvol vdevs on LUN expansion"

for i in 1 2 3; do
	log_must $ZFS create -V $org_size $VFS/vol$i
done

for type in " " mirror raidz raidz2; do
	log_must $ZPOOL create $TESTPOOL1 $type \
		/dev/zvol/$VFS/vol1 \
		/dev/zvol/$VFS/vol2 \
		/dev/zvol/$VFS/vol3

	typeset autoexp=$(get_pool_prop autoexpand $TESTPOOL1)
	if [[ $autoexp != "off" ]]; then
		log_fail "zpool $TESTPOOL1 autoexpand should off but is $autoexp"
	fi

	typeset prev_size=$(get_pool_prop size $TESTPOOL1)

	for i in 1 2 3; do
		log_must $ZFS set volsize=$exp_size $VFS/vol$i
	done

	for i in 1 2 3; do
		log_must $ZPOOL online -e $TESTPOOL1 /dev/zvol/$VFS/vol$i
	done

	$SYNC
	$SLEEP 10
	$SYNC
	
	# check for zpool history for the pool size expansion
	if [[ $type == " " || $type == "mirror" ]]; then
		$ZPOOL history -il $TESTPOOL1 |  \
			$GREP "pool '$TESTPOOL1' size:" | \
			$GREP "internal vdev online" | \
			$GREP "(+${EX_1GB})" >/dev/null 2>&1
		
		if [[ $? -ne 0 ]]; then
			log_fail "pool $TESTPOOL1" \
				" is not autoexpand after LUN expansion"
		fi
	else 
		$ZPOOL history -il $TESTPOOL1 |  \
			$GREP "pool '$TESTPOOL1' size:" | \
			$GREP "internal vdev online" | \
			$GREP "(+${EX_3GB})" >/dev/null 2>&1
		
		if [[ $? -ne 0 ]] ; then
			log_fail "pool $TESTPOOL1" \
				" is not autoexpand after LUN expansion"
		fi
	fi
		
	typeset expand_size=$(get_pool_prop size $TESTPOOL1)

	log_must $ZPOOL destroy $TESTPOOL1

	for i in 1 2 3; do
		log_must $ZFS set volsize=$org_size $VFS/vol$i
	done
	
done

log_pass "zpool can expand after zpool online -e zvol vdevs on LUN expansion"
