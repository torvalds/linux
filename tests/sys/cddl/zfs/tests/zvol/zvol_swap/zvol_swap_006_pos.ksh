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
# ident	"@(#)zvol_swap_006_pos.ksh	1.4	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zvol_swap_006_pos
#
# DESCRIPTION:
#	 A volume can be add as several segments, but overlapping are not
#	 allowed.
#
# STRATEGY:
#	1. Figure out three groups swaplow and swaplen.
#	2. Verify different volume segments can be added correctly.
#	3. Verify overlapping swap volume are not allowed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-13)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset -i i=0

	while ((count > 0)); do
		log_must $SWAP -d $swapname ${swap_opt[$i]}

		((i += 2))
		((count -= 1))
	done
}

log_assert "Verify volume can be add as several segments, but overlapping " \
	"are not allowed."
log_onexit cleanup

test_requires SWAP

typeset vol=$TESTPOOL/$TESTVOL
typeset -i pageblocks volblocks
((pageblocks = $($PAGESIZE) / 512))
((volblocks = $(get_prop volsize $vol) / 512))

log_note "Verify volume can be add as several segments."

#
#		swaplow			swaplen
set -A swap_opt	$((pageblocks))	    \
		$((pageblocks * ((RANDOM % 50) + 1) + (RANDOM % pageblocks) )) \
		$((volblocks / 3))  \
		$((pageblocks * ((RANDOM % 50) + 1) + (RANDOM % pageblocks) )) \
		$((volblocks / 2))  \
		$((pageblocks * ((RANDOM % 50) + 1) + (RANDOM % pageblocks) )) \
		$(((volblocks*2) / 3))  \
		$((pageblocks * ((RANDOM % 50) + 1) + (RANDOM % pageblocks) )) 

swapname=/dev/zvol/$vol
typeset -i i=0 count=0

if is_swap_inuse $swapname ; then
	log_must $SWAP -d $swapname
fi

while ((i < ${#swap_opt[@]})); do 
	log_must $SWAP -a $swapname ${swap_opt[$i]} ${swap_opt[((i+1))]}

	((i += 2))
	((count += 1))
done

log_note "Verify overlapping swap volume are not allowed"
i=0
while ((i < ${#swap_opt[@]})); do
	log_mustnot $SWAP -a $swapname ${swap_opt[$i]}

	((i += 2))
done

log_pass "Verify volume can be added as several segments passed."
