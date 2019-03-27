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
# ident	"@(#)zpool_scrub_003_pos.ksh	1.2	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_scrub_003_pos
#
# DESCRIPTION:
#	scrub command terminates the existing scrub process and starts
#	a new scrub.
#
# STRATEGY:
#	1. Setup a pool and fill with data
#	2. Kick off a scrub
#	3. Check the completed percent and invoke another scrub
#	4. Check the percent again, verify a new scrub started.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function get_scrub_percent
{
	typeset -i percent
        percent=$($ZPOOL status $TESTPOOL | $GREP "^ scrub" | \
        	$AWK '{print $7}' | $AWK -F. '{print $1}') 
        if is_pool_scrubbed $TESTPOOL ; then
                percent=100
        fi
	$ECHO $percent
}

log_assert "scrub command terminates the existing scrub process and starts" \
	"a new scrub."

log_must $ZPOOL scrub $TESTPOOL
typeset -i PERCENT=30 percent=0
while ((percent < PERCENT)) ; do
	percent=$(get_scrub_percent)
done

log_must $ZPOOL scrub $TESTPOOL
percent=$(get_scrub_percent)
if ((percent > PERCENT)); then
	log_fail "zpool scrub don't stop existing scrubbing process."
fi

log_pass "scrub command terminates the existing scrub process and starts" \
	"a new scrub."
