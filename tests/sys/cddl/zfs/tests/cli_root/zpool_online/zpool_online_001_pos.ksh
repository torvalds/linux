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
# ident	"@(#)zpool_online_001_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_online_001
#
# DESCRIPTION:
# Executing 'zpool online' with valid parameters succeeds.
#
# STRATEGY:
# 1. Create an array of correctly formed 'zpool online' options
# 2. Execute each element of the array.
# 3. Verify use of each option is successful.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-09-30)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)

set -A args ""

function cleanup
{
	#
	# Ensure we don't leave disks in temporary online state (-t)
	#
	for disk in $DISKLIST; do
		log_must $ZPOOL online $TESTPOOL $disk
		check_state $TESTPOOL $disk "online"
		if [[ $? != 0 ]]; then
			log_fail "Unable to online $disk"
		fi

	done	
}

log_assert "Executing 'zpool online' with correct options succeeds"

log_onexit cleanup

if [[ -z $DISKLIST ]]; then
	log_fail "DISKLIST is empty."
fi

typeset -i i=0

for disk in $DISKLIST; do
	i=0
	while [[ $i -lt ${#args[*]} ]]; do
		log_must $ZPOOL offline $TESTPOOL $disk
		check_state $TESTPOOL $disk "offline"
		if [[ $? != 0 ]]; then
			log_fail "$disk of $TESTPOOL did not match offline state"
		fi

		log_must $ZPOOL online ${args[$i]} $TESTPOOL $disk
		check_state $TESTPOOL $disk "online"
		if [[ $? != 0 ]]; then
			log_fail "$disk of $TESTPOOL did not match online state"
		fi

		(( i = i + 1 ))
	done
done

log_note "Issuing repeated 'zpool online' commands succeeds."

typeset -i iters=20
typeset -i index=0

for disk in $DISKLIST; do
        i=0
        while [[ $i -lt $iters ]]; do
		index=`expr $RANDOM % ${#args[*]}`
                log_must $ZPOOL online ${args[$index]} $TESTPOOL $disk
                check_state $TESTPOOL $disk "online"
                if [[ $? != 0 ]]; then
                        log_fail "$disk of $TESTPOOL did not match online state"
                fi

                (( i = i + 1 ))
        done
done

log_pass "'zpool online' with correct options succeeded"
