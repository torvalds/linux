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
# ident	"@(#)zfs_receive_009_neg.ksh	1.2	07/10/09 SMI"
#

. $STF_SUITE/tests/cli_root/cli_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_009_neg
#
# DESCRIPTION:
#	Verify 'zfs receive' fails with bad options, missing argument or too many 
#	arguments.
#
# STRATEGY:
#	1. Set a array of illegal arguments
#	2. Execute 'zfs receive' with illegal arguments
#	3. Verify the command should be failed
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	typeset ds

	if snapexists $snap; then
		log_must $ZFS destroy $snap
	fi
	for ds in $ctr1 $ctr2 $fs1; do
		if datasetexists $ds; then
			log_must $ZFS destroy -rf $ds
		fi
	done
	if [[ -d $TESTDIR2 ]]; then
		$RM -rf $TESTDIR2
	fi
}

log_assert "Verify 'zfs receive' fails with bad option, missing or too many arguments" 
log_onexit cleanup

set -A badopts "v" "n" "F" "d" "-V" "-N" "-f" "-D" "-VNfD" "-vNFd" "-vnFD" "-dVnF" \
		"-vvvNfd" "-blah" "-12345" "-?" "-*" "-%" 
set -A validopts "" "-v" "-n" "-F" "-vn" "-nF" "-vnF" "-vd" "-nd" "-Fd" "-vnFd"

ctr1=$TESTPOOL/$TESTCTR1
ctr2=$TESTPOOL/$TESTCTR2
fs1=$TESTPOOL/$TESTFS1
fs2=$TESTPOOL/$TESTFS2
fs3=$TESTPOOL/$TESTFS3
snap=$TESTPOOL/$TESTFS@$TESTSNAP
bkup=$TESTDIR2/bkup.${TESTCASE_ID}

# Preparations for negative testing
for ctr in $ctr1 $ctr2; do
	log_must $ZFS create $ctr
done
if [[ -d $TESTDIR2 ]]; then
	$RM -rf $TESTDIR2
fi
log_must $ZFS create -o mountpoint=$TESTDIR2 $fs1
log_must $ZFS snapshot $snap
log_must eval "$ZFS send $snap > $bkup"

#Testing zfs receive fails with input from terminal
log_mustnot eval "$ZFS recv $fs3 </dev/console"

# Testing with missing argument and too many arguments
typeset -i i=0
while (( i < ${#validopts[*]} )); do
	log_mustnot eval "$ZFS recv < $bkup"

	$ECHO ${validopts[i]} | $GREP "d" >/dev/null 2>&1
	if (( $? != 0 )); then
		log_mustnot eval "$ZFS recv ${validopts[i]} $fs2 $fs3 < $bkup"
	else
		log_mustnot eval "$ZFS recv ${validopts[i]} $ctr1 $ctr2 < $bkup"
	fi

	(( i += 1 ))
done

# Testing with bad options
i=0
while (( i < ${#badopts[*]} ))
do
	log_mustnot eval "$ZFS recv ${badopts[i]} $ctr1 < $bkup"
	log_mustnot eval "$ZFS recv ${badopts[i]} $fs2 < $bkup"
	
	(( i = i + 1 ))
done

log_pass "'zfs receive' as expected with bad options, missing or too many arguments."
