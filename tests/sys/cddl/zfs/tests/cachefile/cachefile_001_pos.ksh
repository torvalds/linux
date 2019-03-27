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
# ident	"@(#)cachefile_001_pos.ksh	1.1	08/02/29 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cachefile/cachefile.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: cachefile_001_pos
#
# DESCRIPTION:
#
# Creating a pool with "cachefile" set doesn't update zpool.cache
#
# STRATEGY:
# 1. Create a pool with the cachefile property set
# 2. Verify that the pool doesn't have an entry in zpool.cache
# 3. Verify the cachefile property is set
# 4. Create a pool without the cachefile property
# 5. Verify the cachefile property isn't set
# 6. Verify that zpool.cache contains an entry for the pool
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-09-05)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	typeset file

	destroy_pool $TESTPOOL
	for file in $CPATH1 $CPATH2 ; do
		if [[ -f $file ]] ; then
			log_must $RM $file
		fi
	done
}

verify_runnable "global"

log_assert "Creating a pool with \"cachefile\" set doesn't update zpool.cache"
log_onexit cleanup

CPATHARG="-"
set -A opts "none" "false" "none" \
	"$CPATH" "true" "$CPATHARG" \
	"$CPATH1" "true" "$CPATH1" \
	"$CPATH2" "true" "$CPATH2"

typeset -i i=0

while (( i < ${#opts[*]} )); do
	log_must $ZPOOL create -o cachefile=${opts[i]} $TESTPOOL $DISKS
	case ${opts[((i+1))]} in
		false) log_mustnot pool_in_cache $TESTPOOL
			;;
		true) log_must pool_in_cache $TESTPOOL ${opts[i]}
			;;
	esac

	PROP=$(get_pool_prop cachefile $TESTPOOL)
	if [[ $PROP != ${opts[((i+2))]} ]]; then
		log_fail "cachefile property not set as expected. " \
			"Expect: ${opts[((i+2))]}, Current: $PROP"
	fi
	log_must $ZPOOL destroy $TESTPOOL
	(( i = i + 3 ))
done
		
log_pass "Creating a pool with \"cachefile\" set doesn't update zpool.cache"

