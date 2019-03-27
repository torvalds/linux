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
# ident	"@(#)user_property_004_pos.ksh	1.4	09/05/19 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: user_property_004_neg
#
# DESCRIPTION:
#	User property has no effect to snapshot until 'Snapshot properties' supported.
#
# STRATEGY:
#	1. Verify user properties could be transformed by 'zfs snapshot'
#	2. Verify user properties could be set upon snapshot.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
		typeset fssnap=$fs@snap
		if datasetexists $fssnap ; then
			log_must $ZFS destroy -f $fssnap
		fi
	done
	cleanup_user_prop $TESTPOOL
}

function nonexist_user_prop
{
	typeset user_prop=$1
	typeset dtst=$2

	typeset source=$(get_source $user_prop $dtst)
	typeset value=$(get_prop $user_prop $dtst)
	if [[ $source == '-' && $value == '-' ]]; then
		return 0
	else
		return 1
	fi
}

log_assert "User property has no effect to snapshot until 'Snapshot properties' supported."
log_onexit cleanup

typeset snap_property=

$ZPOOL upgrade -v | $GREP "Snapshot properties" > /dev/null 2>&1
if (( $? == 0 )) ; then
	snap_property="true"
fi

for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
	typeset fssnap=$fs@snap
	prop_name=$(valid_user_property 10)
	value=$(user_property_value 16)
	log_must eval "$ZFS set $prop_name='$value' $fs"
	log_must eval "check_user_prop $fs $prop_name '$value'"

	log_must $ZFS snapshot $fssnap

	if [[ -n $snap_property ]] ; then
		log_mustnot nonexist_user_prop $prop_name $fssnap

		log_must eval "$ZFS set $prop_name='$value' $fssnap"
		log_mustnot nonexist_user_prop $prop_name $fssnap
	else
		log_must nonexist_user_prop $prop_name $fssnap
		log_mustnot eval "$ZFS set $prop_name='$value' $fssnap"
		log_must nonexist_user_prop $prop_name $fssnap
	fi
done

log_pass "User properties has effect upon snapshot."
