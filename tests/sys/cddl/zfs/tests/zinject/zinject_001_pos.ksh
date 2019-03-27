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
# ident	"@(#)zinject_001_pos.ksh	1.3	09/06/22 SMI"
#

###############################################################################
#
# __stc_assertion_start
#
# ID: zinject_001_pos
#
# DESCRIPTION:
#
# Inject an error into the plain file contents of a file.
# Verify fmdump will get the expect ereport
#
# STRATEGY:
# 1) Populate ZFS file system
# 2) Inject an error into the plain file contents of a file.
# 3) Verify fmdump get the ereport as expect.
#	<Errno>		<Expect ereport>		<Comments>	
#	io 		ereport.fs.zfs.io
#			ereport.fs.zfs.data
#	checksum	ereport.fs.zfs.checksum		Non-stripe pool
#			ereport.fs.zfs.data
#	checksum	ereport.fs.zfs.data		Stripe pool only
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-02-01)
#
# __stc_assertion_end
#
################################################################################

. $STF_SUITE/tests/zinject/zinject.kshlib

verify_runnable "global"

log_assert "Verify fault inject handle content error successfully."
log_onexit cleanup_env

set -A types "" "mirror" "raidz" "raidz2"

typeset -i maxnumber=300

function test_zinject_unit
{
	typeset etype=$1
	typeset object=$2
	typeset errno=$3
	typeset ereport=$4
	typeset now

	typeset otype="file"
	[[ -d $object ]] && otype="dir"

	now=`date '+%m/%d/%y %H:%M:%S'`
	inject_fault $etype $object $errno

	trigger_inject $etype $object $otype

	log_must check_ereport "$now" $ereport

	log_must check_status $TESTPOOL $object

	inject_clear
}

function test_zinject
{
	typeset basedir=$1
	typeset pooltype=$2
	typeset -i i=0
	typeset etype="data"

	set -A errset "io" "ereport.fs.zfs.io ereport.fs.zfs.data"

	((i=${#errset[*]}))
	if [[ -n $pooltype ]] ; then
		errset[i]="checksum"
		errset[((i+1))]="ereport.fs.zfs.checksum ereport.fs.zfs.data"
	else
		errset[i]="checksum"
		errset[((i+1))]="ereport.fs.zfs.data"
	fi
		
	i=0
	while ((i < ${#errset[*]} )); do

		for object in $basedir/testfile.$maxnumber \
			$basedir/testdir.$maxnumber ; do
		
			test_zinject_unit $etype $object \
				${errset[i]} "${errset[((i+1))]}"
		done

		(( i = i + 2 ))
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
	test_zinject $TESTDIR $type

	cleanup_env
done

log_pass "Fault inject handle content error successfully."
