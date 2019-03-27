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
# ident	"@(#)zfs_clone_001_neg.ksh	1.4	09/01/13 SMI"	
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_clone_001_neg
#
# DESCRIPTION: 
#	'zfs clone' should fail with inapplicable scenarios, including:
#		* Null arguments
#		* non-existant snapshots.
#		* invalid characters in ZFS namesapec
#		* Leading slash in the target clone name
#		* The argument contains an empty component.
#		* The pool specified in the target doesn't exist.
#		* The parent dataset of the target doesn't exist.
#		* The argument refer to a pool, not dataset.
#		* The target clone already exists.
#		* Null target clone argument.
#		* Too many arguments. 
#
# STRATEGY:
#	1. Create an array of parameters
#	2. For each parameter in the array, execute the sub-command
#	3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-25)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

typeset target1=$TESTPOOL/$TESTFS1
typeset target2=$TESTPOOL/$TESTCTR1/$TESTFS1
typeset targets="$target1 $target2 $NONEXISTPOOLNAME/$TESTFS"
 
set -A args "" \
	"$TESTPOOL/$TESTFS@blah $target1" "$TESTPOOL/$TESTVOL@blah $target1" \
	"$TESTPOOL/$TESTFS@blah* $target1" "$TESTPOOL/$TESTVOL@blah* $target1" \
	"$SNAPFS $target1*" "$SNAPFS1 $target1*" \
	"$SNAPFS /$target1" "$SNAPFS1 /$target1" \
	"$SNAPFS $TESTPOOL//$TESTFS1" "$SNAPFS1 $TESTPOOL//$TESTFS1" \
	"$SNAPFS $NONEXISTPOOLNAME/$TESTFS" "$SNAPFS1 $NONEXISTPOOLNAME/$TESTFS" \
	"$SNAPFS" "$SNAPFS1" \
	"$SNAPFS $target1 $target2" "$SNAPFS1 $target1 $target2"
typeset -i argsnum=${#args[*]}
typeset -i j=0
while (( j < argsnum )); do
	args[((argsnum+j))]="-p ${args[j]}"
	((j = j + 1))
done

set -A moreargs "$SNAPFS $target2" "$SNAPFS1 $target2" \
	"$SNAPFS $TESTPOOL" "$SNAPFS1 $TESTPOOL" \
	"$SNAPFS $TESTPOOL/$TESTCTR" "$SNAPFS $TESTPOOL/$TESTFS" \
	"$SNAPFS1 $TESTPOOL/$TESTCTR" "$SNAPFS1 $TESTPOOL/$TESTFS" 

set -A args ${args[*]} ${moreargs[*]}

function setup_all
{
	log_note "Create snapshots and mount them..."

	for snap in $SNAPFS $SNAPFS1 ; do
		if ! snapexists $snap ; then
			log_must $ZFS snapshot $snap
		fi
	done

	return 0
}

function cleanup_all
{
	typeset -i i=0
	
	for fs in $targets; do

        	datasetexists $fs && \
			log_must $ZFS destroy -f $fs

		(( i = i + 1 ))
	done

	for snap in $SNAPFS $SNAPFS1 ; do
		snapexists $snap && \
			log_must $ZFS destroy -Rf $snap
	done

	return 0
}

log_assert "Badly-formed 'zfs clone' with inapplicable scenarios" \
	"should return an error."
log_onexit cleanup_all

setup_all

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot $ZFS clone ${args[i]}
	((i = i + 1))
done

log_pass "Badly formed 'zfs clone' with inapplicable scenarios" \
	"fail as expected."
