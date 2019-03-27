#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)zinject_004_pos.ksh	1.6	09/06/22 SMI"
#

###############################################################################
#
# __stc_assertion_start
#
# ID: zinject_004_pos
#
# DESCRIPTION:
#
# Inject an error into the device of a pool.
# Verify fmdump will get the expect ereport, 
# and the fault class of "fault.fs.zfs.vdev.io" be generated.
#
# STRATEGY:
# 1) Populate ZFS file system
# 2) Inject an error into the device of the pool.
# 3) Verify fmdump get the ereport as expected.
#	<Errno>		<Expect ereport>
#	nxio		ereport.fs.zfs.probe_failure
#	io		ereport.fs.zfs.probe_failure
# 4) Verify the fault class of "fault.fs.zfs.vdev.io" be generated.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-05-31)
#
# __stc_assertion_end
#
################################################################################

. $STF_SUITE/tests/zinject/zinject.kshlib

verify_runnable "global"

log_assert "Verify fault inject handle device error successfully."
log_onexit cleanup_env

set -A types "mirror" "raidz" "raidz2"

typeset -i maxnumber=1

function test_zinject
{
	typeset basedir=$1
	typeset -i i=0
	typeset etype="device"
	typeset fclass="fault.fs.zfs.vdev.io"

	set -A errset \
		"nxio" "ereport.fs.zfs.probe_failure" \
		"io" "ereport.fs.zfs.probe_failure"

	set -A alldevarray $alldevs

	for device in $(random_string alldevarray 1); do
		i=0
		while ((i < ${#errset[*]} )); do
			now=`date '+%m/%d/%y %H:%M:%S'`
			inject_device $device $TESTPOOL ${errset[i]}

			trigger_inject $etype $basedir/testfile.$maxnumber "file"
			log_must check_ereport "$now" ${errset[((i+1))]}
			log_must check_fault "$now" $fclass

			inject_clear

			now=`date '+%m/%d/%y %H:%M:%S'`
			inject_device $device $TESTPOOL ${errset[i]}

			trigger_inject $etype $basedir/testdir.$maxnumber "dir"
			log_must check_ereport "$now" ${errset[((i+1))]}
			log_must check_fault "$now" $fclass

			inject_clear

			(( i = i + 2 ))
		done
	done
}

inject_clear
for type in "${types[@]}"; do 
	create_pool $TESTPOOL $type $pooldevs spare $sparedevs

	log_must $ZPOOL add -f $TESTPOOL log $logdevs
	log_must $ZPOOL add -f $TESTPOOL cache $cachedevs

        log_must $ZPOOL replace $TESTPOOL $VDEV0 $sparedevs
	log_must $ZFS create $TESTPOOL/$TESTFS
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	populate_test_env $TESTDIR $maxnumber
	test_zinject $TESTDIR

	cleanup_env
done

log_pass "Fault inject handle device error successfully."
