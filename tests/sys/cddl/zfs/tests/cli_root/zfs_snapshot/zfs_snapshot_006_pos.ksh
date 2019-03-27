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
# ident	"@(#)zfs_snapshot_006_pos.ksh	1.1	09/05/19 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_snapshot_006_pos
#
# DESCRIPTION:
#	User property could be set via creation time by 'zfs snapshot -o'
#
# STRATEGY:
#	1. Create snapshot and give '-o property=value'
#	2. Verify the snapshot be created and user property have been set.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-04-27)
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
			log_must $ZFS destroy -rf $fssnap
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

log_assert "User property could be set upon snapshot via 'zfs snapshot -o'."
log_onexit cleanup

typeset snap_property=

$ZPOOL upgrade -v | $GREP "Snapshot properties" > /dev/null 2>&1
if (( $? != 0 )) ; then
	log_unsupported "Snapshot properties not supported on current system."
fi

for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
	typeset fssnap=$fs@snap
	prop_name=$(valid_user_property 10)
	value=$(user_property_value 16)

	log_must eval "$ZFS snapshot -o $prop_name='$value' $fssnap"
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap

	log_must $ZFS destroy -f $fssnap

	prop_name2=$(valid_user_property 10)
	value2=$(user_property_value 16)

	log_must eval "$ZFS snapshot -o $prop_name='$value' -o $prop_name2='$value2' $fssnap"
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap
	log_mustnot nonexist_user_prop $prop_name2 $fssnap
done

cleanup

prop_name=$(valid_user_property 10)
value=$(user_property_value 16)

log_must eval "$ZFS snapshot -r -o $prop_name='$value' $TESTPOOL@snap"
for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
	typeset fssnap=$fs@snap
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap

	log_must $ZFS destroy -rf $fssnap
done

cleanup

prop_name2=$(valid_user_property 10)
value2=$(user_property_value 16)

log_must eval "$ZFS snapshot -r -o $prop_name='$value' -o $prop_name2='$value2' $TESTPOOL@snap"
for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
	typeset fssnap=$fs@snap
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap
	log_mustnot nonexist_user_prop $prop_name2 $fssnap

	log_must $ZFS destroy -rf $fssnap
done

log_pass "User property could be set upon snapshot via 'zfs snapshot -o'."
