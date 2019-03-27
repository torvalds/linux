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
# ident	"@(#)zinject_003_pos.ksh	1.3	09/06/22 SMI"
#

###############################################################################
#
# __stc_assertion_start
#
# ID: zinject_003_pos
#
# DESCRIPTION:
#
# Inject an error into the first metadnode in the block
# Verify the filesystem unmountable since dnode be injected.
#
# STRATEGY:
# 1) Populate ZFS file system
# 2) Inject an error into the first metadnode in the block.
# 3) Verify the filesystem unmountable, 
#	and 'zpool status -v' will display the error as expect.
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

log_assert "Verify fault inject handle into first metadnode " \
	"cause filesystem unmountable."
log_onexit cleanup_env

set -A types "" "mirror" "raidz" "raidz2"

typeset -i maxnumber=1

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
	inject_fault $etype $object $errno 1

	unmounted $TESTPOOL/$TESTFS || \
		log_fail "$TESTPOOL/$TESTFS mount unexpected."

	log_must check_status $TESTPOOL "$TESTPOOL/$TESTFS:<0x0>" 

	inject_clear

	log_must $ZFS mount -a
}

function test_zinject
{
	typeset basedir=$1
	typeset pooltype=$2
	typeset -i i=0
	typeset etype="dnode"

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

	populate_test_env $TESTDIR/bad_dir $maxnumber

	test_zinject $TESTDIR/bad_dir $type

	cleanup_env
done

log_pass "Fault inject handle into first metadnode " \
	"cause filesystem unmountable."
