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
# ident	"@(#)cachefile_003_pos.ksh	1.1	08/02/29 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cachefile/cachefile.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: cachefile_003_pos
#
# DESCRIPTION:
#
# Setting altroot=<path> and cachefile=$CPATH for zpool create is succeed
#
# STRATEGY:
# 1. Attempt to create a pool with -o altroot=<path> -o cachefile=<value>
# 2. Verify the command succeed
#
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-09-10)
#
# __stc_assertion_end
#
################################################################################

TESTDIR=/altdir.${TESTCASE_ID}

function cleanup
{
	typeset file

	destroy_pool $TESTPOOL

        for file in $CPATH1 $CPATH2 ; do
                if [[ -f $file ]] ; then
                        log_must $RM $file
                fi
        done

	if [ -d $TESTDIR ]
	then
		$RMDIR $TESTDIR
	fi
}

verify_runnable "global"

log_assert "Setting altroot=path and cachefile=$CPATH for zpool create succeed."
log_onexit cleanup

typeset -i i=0

CPATHARG="-"
set -A opts "none" "none" \
	"$CPATH" "$CPATHARG" \
	"$CPATH1" "$CPATH1" \
	"$CPATH2" "$CPATH2"


while (( i < ${#opts[*]} )); do
	log_must $ZPOOL create -o altroot=$TESTDIR -o cachefile=${opts[i]} \
		$TESTPOOL $DISKS
	if [[ ${opts[i]} != none ]]; then
		log_must pool_in_cache $TESTPOOL ${opts[i]}
	else
		log_mustnot pool_in_cache $TESTPOOL
	fi

	PROP=$(get_pool_prop cachefile $TESTPOOL)
	if [[ $PROP != ${opts[((i+1))]} ]]; then
		log_fail "cachefile property not set as expected. " \
			"Expect: ${opts[((i+1))]}, Current: $PROP"
	fi
	log_must $ZPOOL destroy -f $TESTPOOL
	(( i = i + 2 ))
done

log_pass "Setting altroot=path and cachefile=$CPATH for zpool create succeed."

