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
# ident	"@(#)pool_names_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: pool_names_001_pos
#
# DESCRIPTION:
#
# Test that a set of valid names can be used to create pools. Further 
# verify that the created pools can be destroyed.
#
# STRATEGY:
# 1) For each valid character in the character set, try to create
# and destroy the pool.
# 2) Given a list of valid pool names, try to create and destroy
# pools with the given names.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-11-21)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Ensure that pool names can use the ASCII subset of UTF-8"

function cleanup
{
	[[ -n "$name" ]] && destroy_pool $name

	if [[ -d $TESTDIR ]]; then
		log_must $RM -rf $TESTDIR
	fi

}

log_onexit cleanup

if [[ ! -e $TESTDIR ]]; then
	log_must $MKDIR $TESTDIR
fi

log_note "Ensure letters of the alphabet are allowable"

typeset name=""

for name in A B C D E F G H I J K L M \
    N O P Q R S T U V W X Y Z \
    a b c d e f g h i j k l m \
    n o p q r s t u v w x y z
do
	log_must $ZPOOL create -m $TESTDIR $name $DISK
	if ! poolexists $name; then
		log_fail "Could not create a pool called '$name'"
	fi

	log_must $ZPOOL destroy $name
done

log_note "Ensure a variety of unusual names passes"

name=""

for name in "a.............................." "a_" "a-" "a:" \
    "a." "a123456" "bc0t0d0" "m1rr0r_p00l" "ra1dz_p00l" \
    "araidz2" "C0t2d0" "cc0t0" "raid2:-_." "mirr_:-." \
    "m1rr0r-p00l" "ra1dz-p00l" "spar3_p00l" \
    "spar3-p00l" "hiddenmirrorpool" "hiddenraidzpool" \
    "hiddensparepool"
do
	log_must $ZPOOL create -m $TESTDIR $name $DISK
	if ! poolexists $name; then
		log_fail "Could not create a pool called '$name'"
	fi

	#
	# Since the naming convention applies to datasets too,
	# create datasets with the same names as above.
	#
	log_must $ZFS create $name/$name
	log_must $ZFS snapshot $name/$name@$name
	log_must $ZFS clone $name/$name@$name $name/clone_$name
	log_must $ZFS create -V 150m $name/$name/$name

	log_must $ZPOOL destroy $name
done

log_pass "Valid pool names were accepted correctly."
