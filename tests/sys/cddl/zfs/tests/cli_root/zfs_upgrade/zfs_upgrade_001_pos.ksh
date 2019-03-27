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
# ident	"@(#)zfs_upgrade_001_pos.ksh	1.2	08/08/15 SMI"
#
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_upgrade/zfs_upgrade.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_upgrade_001_pos
#
# DESCRIPTION:
# 	Executing 'zfs upgrade' command succeeds, it should report 
#	the current system version and list all old-version filesystems.
#	If no old-version filesystems be founded, it prints out
#	"All filesystems are formatted with the current version."
#
# STRATEGY:
# 1. Prepare a set of datasets which contain old-version and current version.
# 2. Execute 'zfs upgrade', verify return 0, and it prints out
#	the current system version and list all old-version filesystems.
# 3. Remove all old-version filesystems, then execute 'zfs upgrade' again,
#	verify return 0, and get the expected message.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-25)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if datasetexists $rootfs ; then
		log_must $ZFS destroy -Rf $rootfs
	fi
	log_must $ZFS create $rootfs

	for file in $output $oldoutput ; do
		if [[ -f $file ]]; then
			log_must $RM -f $file
		fi
	done
}

log_assert "Executing 'zfs upgrade' command succeeds."
log_onexit cleanup

rootfs=$TESTPOOL/$TESTFS
typeset output=$TMPDIR/zfs-versions.${TESTCASE_ID}
typeset oldoutput=$TMPDIR/zfs-versions-old.${TESTCASE_ID}
typeset expect_str1="This system is currently running ZFS filesystem version"
typeset expect_str2="All filesystems are formatted with the current version"
typeset expect_str3="The following filesystems are out of date, and can be upgraded"
typeset -i COUNT OLDCOUNT

$ZFS upgrade | $NAWK '$1 ~ "^[0-9]+$" {print $2}'> $oldoutput
OLDCOUNT=$( $WC -l $oldoutput | $AWK '{print $1}' )

old_datasets=""
for version in $ZFS_ALL_VERSIONS ; do
	typeset verfs
	eval verfs=\$ZFS_VERSION_$version
	typeset current_fs=$rootfs/$verfs
	typeset current_snap=${current_fs}@snap
	typeset current_clone=$rootfs/clone$verfs
	log_must $ZFS create -o version=${version} ${current_fs}
	log_must $ZFS snapshot ${current_snap}
	log_must $ZFS clone ${current_snap} ${current_clone}

	if (( version != $ZFS_VERSION )); then
		old_datasets="$old_datasets ${current_fs} ${current_clone}"
	fi
done

if is_global_zone; then
	log_must $ZFS create -V 100m $rootfs/$TESTVOL
fi

log_must eval '$ZFS upgrade > $output 2>&1'

# we also check that the usage message contains at least a description 
# of the current ZFS version.
log_must eval '$GREP "${expect_str1} $ZFS_VERSION" $output > /dev/null 2>&1'
$ZFS upgrade | $NAWK '$1 ~ "^[0-9]+$" {print $2}'> $output
COUNT=$( $WC -l $output | $AWK '{print $1}' )

typeset -i i=0
for fs in ${old_datasets}; do
	log_must $GREP "^$fs$" $output
	(( i = i + 1 ))
done

if (( i != COUNT - OLDCOUNT )); then
	$CAT $output
	log_fail "More old-version filesystems print out than expect."
fi

for fs in $old_datasets ; do
	if datasetexists $fs ; then
		log_must $ZFS destroy -Rf $fs
	fi
done

log_must eval '$ZFS upgrade > $output 2>&1'
log_must eval '$GREP "${expect_str1} $ZFS_VERSION" $output > /dev/null 2>&1'
if (( OLDCOUNT == 0 )); then
	log_must eval '$GREP "${expect_str2}" $output > /dev/null 2>&1'
else
	log_must eval '$GREP "${expect_str3}" $output > /dev/null 2>&1'
fi
$ZFS upgrade | $NAWK '$1 ~ "^[0-9]+$" {print $2}'> $output
COUNT=$( $WC -l $output | $AWK '{print $1}' )

if (( COUNT != OLDCOUNT )); then
	$CAT $output
	log_fail "Unexpect old-version filesystems print out."
fi

log_pass "Executing 'zfs upgrade' command succeeds."
