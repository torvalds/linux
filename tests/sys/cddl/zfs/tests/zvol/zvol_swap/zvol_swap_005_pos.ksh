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
# ident	"@(#)zvol_swap_005_pos.ksh	1.2	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zvol_swap_005_pos
#
# DESCRIPTION:
#	swaplow + swaplen must be less than or equal to the volume size.
#
# STRATEGY:
#	1. Get test system page size and test volume size.
#	2. Random get swaplow and swaplen.
#	3. Verify swap -a should succeed when swaplow + swaplen <= volume size.
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

log_assert "swaplow + swaplen must be less than or equal to the volume size."

test_requires SWAP

typeset vol=$TESTPOOL/$TESTVOL
typeset -i pageblocks volblocks
#
# Both swaplow and swaplen are the desired length of
# the swap area in 512-byte blocks.
#
((pageblocks = $($PAGESIZE) / 512))
((volblocks = $(get_prop volsize $vol) / 512))

typeset -i i=0
while ((i < 10)) ; do
	while true; do
		((swaplow = RANDOM % volblocks))
		# Upwards increment
		((swaplow += pageblocks))
		((swaplow -= (swaplow % pageblocks)))

		# At lease one page size was left for swap area
		((swaplow != volblocks)) && break
	done

	while true; do
		((swaplen = RANDOM % (volblocks - swaplow)))
		# Downward increment
		((swaplen -= (swaplen % pageblocks)))

		# At lease one page size was left for swap area
		((swaplen != 0)) && break
	done

	# The minimum swap size should be 2 pagesize.
	((swaplow + swaplen < pageblocks * 2)) && continue

	swapname="/dev/zvol/$vol"
	if is_swap_inuse $swapname ; then
		log_must $SWAP -d $swapname
	fi

	log_must $SWAP -a $swapname $swaplow $swaplen
	log_must $SWAP -d $swapname $swaplow

	((i += 1))
done

log_pass "Verify swaplow + swaplen must be less than or equal to volsize passed."
