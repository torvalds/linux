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
# ident	"@(#)zfs_snapshot_001_neg.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_snapshot_001_neg
#
# DESCRIPTION: 
#	Try each 'zfs snapshot' with inapplicable scenarios to make sure
#	it returns an error. include:
#		* No arguments given.
#		* The argument contains invalid characters for the ZFS namesapec
#		* Leading slash in snapshot name
#		* The argument contains an empty component.
#		* Missing '@' delimiter.
#		* Multiple '@' delimiters in snapshot name.
#		* The snapshot already exist.
#		* Create snapshot upon the pool. 
#			(Be removed since pool is treated as filesystem as well)
#		* Create snapshot upon a non-existent filesystem.
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
# CODING_STATUS: COMPLETED (2005-07-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

set -A args "" \
	"$TESTPOOL/$TESTFS@blah*" "$TESTPOOL/$TESTFS@blah?" \
	"$TESTPOOL/$TESTVOL@blah*" "$TESTPOOL/$TESTVOL@blah?" \
	"/$TESTPOOL/$TESTFS@$TESTSNAP" "/$TESTPOOL/$TESTVOL@$TESTSNAP" \
	"@$TESTSNAP" "$TESTPOOL/$TESTFS@" "$TESTPOOL/$TESTVOL@" \
	"$TESTPOOL//$TESTFS@$TESTSNAP" "$TESTPOOL//$TESTVOL@$TESTSNAP" \
	"$TESTPOOL/$TESTFS/$TESTSNAP" "$TESTPOOL/$TESTVOL/$TESTSNAP" \
	"$TESTPOOL/$TESTFS@$TESTSNAP@$TESTSNAP1" \
	"$TESTPOOL/$TESTVOL@$TESTSNAP@$TESTSNAP1" \
	"$SNAPFS" "$SNAPFS1" \
	"blah/blah@$TESTSNAP"

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
	
	while (( i < ${#args[*]} )); do

		for snap in ${args[i]}; do
        		snapexists $snap && \
				log_must $ZFS destroy -f $snap

		done

		(( i = i + 1 ))
	done

	for mtpt in $SNAPDIR $SNAPDIR1 ; do
		[[ -d $mtpt ]] && \
			log_must $RM -rf $mtpt
	done

	return 0
}

log_assert "Badly-formed 'zfs snapshot' with inapplicable scenarios " \
	"should return an error."
log_onexit cleanup_all

setup_all

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot $ZFS snapshot ${args[i]}
	((i = i + 1))
done

log_pass "Badly formed 'zfs snapshot' with inapplicable scenarios " \
	"fail as expected."
