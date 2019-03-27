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
# ident	"@(#)zvol_swap_004_pos.ksh	1.1	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zvol_swap_004_pos
#
# DESCRIPTION:
#	The minimum volume size for swap should be a multiple of 2 pagesize
#	bytes.
#
# STRATEGY:
#	1. Get test system page size.
#	2. Create different size volumes.
#	3. Verify 'swap -a' has correct behaviour.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-12)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset tmp
	for tmp in $swaplist ; do
		log_must $SWAP -d $tmp
	done
	for tmp in $vollist ; do
		log_must $ZFS destroy $tmp
	done
}

log_assert "The minimum volume size should be a multiple of 2 pagesize bytes."
log_onexit cleanup

test_requires SWAP

typeset -i volblksize pagesize=$($PAGESIZE)
((volblksize = pagesize / 2))
#
#	volume size for swap		Expected results
#
set -A array	\
	$((volblksize))			"fail"	\
	$((2 * volblksize))		"fail"	\
	$((3 * volblksize))		"fail"	\
	$((4 * volblksize))		"pass"	\
	$((5 * volblksize))		"pass"	\
	$((6 * volblksize))		"pass"

typeset -i i=0
while ((i < ${#array[@]})); do
	vol="$TESTPOOL/vol_${array[$i]}"
	vollist="$vollist $vol"
	
	log_must $ZFS create -b $volblksize -V ${array[$i]} $vol

	swapname="/dev/zvol/$vol"
	if [[ ${array[((i+1))]} == "fail" ]]; then
		log_mustnot $SWAP -a $swapname
	else
		log_must $SWAP -a $swapname
		swaplist="$swaplist $swapname"
	fi

	((i += 2))
done

log_pass "Verify the minimum volume size pass."
