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
# ident	"@(#)zpool_destroy_002_pos.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_destroy_002_pos
#
# DESCRIPTION: 
#	'zpool destroy -f <pool>' can forcely destroy the specified pool.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Create some datasets within the pool
#	3. Change directory to any mountpoint of these datasets, 
#	   Verify 'zpool destroy' without '-f' will fail.
#	4. 'zpool destroy -f' the pool
#	5. Verify the pool is destroyed successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
###############################################################################

verify_runnable "global"

function cleanup
{
	[[ -n $cwd ]] && log_must cd $cwd

	if [[ -d $TESTDIR ]]; then
		ismounted $TESTDIR 
		((  $? == 0 )) && \
			log_must $UNMOUNT $TESTDIR
		log_must $RM -rf $TESTDIR
	fi

	destroy_pool $TESTPOOL
}

set -A datasets "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR/$TESTFS1" \
	"$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTVOL" \

log_assert "'zpool destroy -f <pool>' can forcely destroy the specified pool" 

log_onexit cleanup

typeset cwd=""

create_pool "$TESTPOOL" "$DISK"
log_must $ZFS create $TESTPOOL/$TESTFS
log_must $MKDIR -p $TESTDIR
log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must $ZFS create $TESTPOOL/$TESTCTR
log_must $ZFS create $TESTPOOL/$TESTCTR/$TESTFS1
log_must $ZFS create -V $VOLSIZE $TESTPOOL/$TESTVOL

typeset -i i=0
while (( $i < ${#datasets[*]} )); do
	datasetexists "${datasets[i]}" || \
		log_fail "Create datasets fail."
	((i = i + 1))
done

cwd=$PWD
log_note "'zpool destroy' without '-f' will fail " \
	"while pool is busy."

for dir in $TESTDIR /$TESTPOOL/$TESTCTR /$TESTPOOL/$TESTCTR/$TESTFS1 ; do
	log_must cd $dir
	log_mustnot $ZPOOL destroy $TESTPOOL

	# Need mount here, otherwise some dataset may be unmounted.
	log_must $ZFS mount -a

	i=0
	while (( i < ${#datasets[*]} )); do
		datasetexists "${datasets[i]}" || \
			log_fail "Dataset ${datasets[i]} removed unexpected."
		((i = i + 1))
	done
done

destroy_pool $TESTPOOL
log_mustnot poolexists "$TESTPOOL"

log_pass "'zpool destroy -f <pool>' success."
