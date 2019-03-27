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
# ident	"@(#)zfs_send_004_neg.ksh	1.5	08/02/27 SMI"
#

. $STF_SUITE/tests/cli_root/cli_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_send_004_neg
#
# DESCRIPTION:
#	Verify 'zfs send' fails with malformed parameters.
#
# STRATEGY:
#	1. Define malformed parameters in array
#	2. Feed the parameters to 'zfs send' 
#	3. Verify the result
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-06)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset snap f 

	for snap in $snap1 $snap2 $snap3; do
		snapexists $snap && \
			log_must $ZFS destroy -f $snap
	done

	for f in $tmpfile1 $tmpfile2; do
		if [[ -e $f ]]; then 
			$RM -f $f
		fi
	done
}

fs=$TESTPOOL/$TESTFS
snap1=$fs@snap1
snap2=$fs@snap2
snap3=$fs@snap3

set -A badargs \
	"" "$TESTPOOL" "$TESTFS" "$fs" "$fs@nonexisten_snap" "?" \
	"$snap1/blah" "$snap1@blah" "-i" "-x" "-i $fs" \
	"-x $snap1 $snap2" "-i $snap1" \
	"-i $snap2 $snap1" "$snap1 $snap2" "-i $snap1 $snap2 $snap3" \
	"-ii $snap1 $snap2" "-iii $snap1 $snap2" " -i $snap2 $snap1/blah" \
	"-i $snap2/blah $snap1" \
	"-i $snap2/blah $snap1/blah" \
	"-i $snap1 blah@blah" \
	"-i blah@blah $snap1" \
	"-i $snap1 ${snap2##*@}" "-i $snap1 @${snap2##*@}" \
	"-i ${snap1##*@} ${snap2##*@}" "-i @${snap1##*@} @${snap2##*@}" \
	"-i ${snap1##*@} $snap2/blah" "-i @${snap1##*@} $snap2/blah" \
	"-i @@${snap1##*@} $snap2" "-i $snap1 -i $snap1 $snap2" \
	"-i snap1 snap2" "-i $snap1 snap2" \
	"-i $snap1 $snap2 -i $snap1 $snap2" \
	"-i snap1 $snap2 -i snap1 $snap2"

log_assert "Verify that invalid parameters to 'zfs send' are caught." 
log_onexit cleanup

log_must $ZFS snapshot $snap1
tmpfile1=$TESTDIR/testfile1.${TESTCASE_ID}
log_must $TOUCH $tmpfile1
log_must $ZFS snapshot $snap2
tmpfile2=$TESTDIR/testfile2.${TESTCASE_ID}
log_must $TOUCH $tmpfile2
log_must $ZFS snapshot $snap3

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	log_mustnot eval "$ZFS send ${badargs[i]} >/dev/null"
	
	(( i = i + 1 ))
done

#Testing zfs send fails by send backup stream to terminal
for arg in "$snap1" "-i $snap1 $snap2"; do
	log_mustnot eval "$ZFS send $arg >/dev/console"
done

log_pass "Invalid parameters to 'zfs send' are caught as expected."
