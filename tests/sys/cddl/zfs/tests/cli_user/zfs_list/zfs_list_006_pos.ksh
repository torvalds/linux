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
# ident	"@(#)zfs_list_006_pos.ksh	1.1	09/05/19 SMI"
#
. $STF_SUITE/tests/cli_user/zfs_list/zfs_list.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_list_006_pos
#
# DESCRIPTION:
#	Verify 'zfs list' exclude list of snapshot.
#
# STRATEGY:
#	1. Verify snapshot not shown in the list:
#		zfs list [-r]
#	2. Verify snapshot will be shown by following case:
#		zfs list [-r] -t snapshot
#		zfs list [-r] -t all
#		zfs list <snapshot>
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-24)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

if ! pool_prop_exist "listsnapshots" ; then
	log_unsupported "Pool property of 'listsnapshots' not supported."
fi

log_assert "Verify 'zfs list' exclude list of snapshot."

set -A hide_options "--" "-t filesystem" "-t volume"
set -A show_options "--" "-t snapshot" "-t all"

typeset pool=${TESTPOOL%%/*}
typeset	dataset=${DATASETS%% *}
typeset BASEFS=$TESTPOOL/$TESTFS

for newvalue in "" "on" "off" ; do

	if [[ -n $newvalue ]] && ! is_global_zone ; then
		break
	fi

	if [[ -n $newvalue ]] ; then
		log_must $ZPOOL set listsnapshots=$newvalue $pool
	fi

	if [ -z "$newvalue" -o "off" = "$newvalue" ] ; then
		run_unprivileged $ZFS list -r -H -o name $pool | $GREP -q '@' && \
			log_fail "zfs list included snapshots but shouldn't have"
	else
		run_unprivileged $ZFS list -r -H -o name $pool | $GREP -q '@' || \
			log_fail "zfs list failed to include snapshots"
	fi
	
		
	typeset -i i=0
	while (( i < ${#hide_options[*]} )) ; do
		run_unprivileged $ZFS list -r -H -o name ${hide_options[i]} $pool | \
			$GREP -q '@' && \
			log_fail "zfs list included snapshots but shouldn't have"

		(( i = i + 1 ))
	done

	(( i = 0 ))

	while (( i < ${#show_options[*]} )) ; do
		run_unprivileged $ZFS list -r -H -o name ${show_options[i]} $pool | \
			$GREP -q '@' || \
			log_fail "zfs list failed to include snapshots"
		(( i = i + 1 ))
	done

	output=$(run_unprivileged $ZFS list -H -o name $BASEFS/${dataset}@snap)
	if [[ $output != $BASEFS/${dataset}@snap ]] ; then
		log_fail "zfs list not show $BASEFS/${dataset}@snap"
	fi
	
	if is_global_zone ; then
		output=$(run_unprivileged $ZFS list -H -o name $BASEFS/${dataset}-vol@snap)
		if [[ $output != $BASEFS/${dataset}-vol@snap ]] ; then
			log_fail "zfs list not show $BASEFS/${dataset}-vol@snap"
		fi
	fi
done
	
log_pass "'zfs list' exclude list of snapshot."
