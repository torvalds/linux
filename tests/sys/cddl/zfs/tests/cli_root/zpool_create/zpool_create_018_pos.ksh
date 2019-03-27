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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_create_018_pos.ksh	1.4	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_018_pos
#
# DESCRIPTION:
#
# zpool create can create pools with specified properties
#
# STRATEGY:
# 1. Create a pool with all editable properties
# 2. Verify those properties are set
# 3. Create a pool with two properties set
# 4. Verify both properties are set correctly
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-27)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	destroy_pool $TESTPOOL

	if [[ -f $CPATH ]] ; then
		log_must $RM $CPATH
	fi
}

log_onexit cleanup
log_assert "zpool create can create pools with specified properties"

if [[ -n $DISK ]]; then
	disk=$DISK
else
	disk=$DISK0
fi

# we don't include "root" property in this list, as it requires both "cachefile"
# and "root" to be set at the same time. A test for this is included in
# ../../root. 
set -A props "autoreplace" "delegation" "cachefile" "version"
set -A vals  "off"         "off"        "$CPATH"   	    "3"

if pool_prop_exist autoexpand ; then
        set -A props ${props[*]} "autoexpand"
        set -A vals ${vals[*]} "on"
fi

typeset -i i=0;
while [ $i -lt "${#props[@]}" ]
do
	log_must $ZPOOL create -o ${props[$i]}=${vals[$i]} $TESTPOOL $disk
	RESULT=$(get_pool_prop ${props[$i]} $TESTPOOL)
	if [[ $RESULT != ${vals[$i]} ]]
	then
		$ZPOOL get all $TESTPOOL
		log_fail "Pool was created without setting the ${props[$i]} property"
	fi
	destroy_pool $TESTPOOL
        (( i = i + 1 ))
done

# pick two properties, and verify we can create with those as well
log_must $ZPOOL create -o delegation=off -o cachefile=$CPATH $TESTPOOL $disk
RESULT=$(get_pool_prop delegation $TESTPOOL)
if [[ $RESULT != off ]]
then
	$ZPOOL get all $TESTPOOL
	log_fail "Pool created without the delegation prop."
fi

RESULT=$(get_pool_prop cachefile $TESTPOOL)
if [[ $RESULT != $CPATH ]]
then
	$ZPOOL get all $TESTPOOL
	log_fail "Pool created without the cachefile prop."
fi

log_pass "zpool create can create pools with specified properties"
