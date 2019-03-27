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

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2012,2013 Spectra Logic Corporation.  All rights reserved.
# Use is subject to license terms.
# 
# Portions taken from:
# ident	"@(#)replacement_001_pos.ksh	1.4	08/02/27 SMI"
#
# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/include/libgnop.kshlib
. $STF_SUITE/tests/hotspare/hotspare.kshlib
. $STF_SUITE/tests/zfsd/zfsd.kshlib

function cleanup
{
	destroy_pool $TESTPOOL
	[[ -e $TESTDIR ]] && log_must $RM -rf $TESTDIR/*
	for md in $MD0 $MD1 $MD2 $MD3; do
		gnop destroy -f $md
		for ((i=0; i<5; i=i+1)); do
			$MDCONFIG -d -u $md && break
			$SLEEP 1
		done
	done
}

log_assert "ZFSD will correctly replace disks that disappear and reappear \
	   with different devnames"

# Outline
# Use gnop on top of file-backed md devices
# * file-backed md devices so we can destroy them and recreate them with
#   different devnames
# * gnop so we can destroy them while still in use
# Create a double-parity pool
# Remove two vdevs
# Destroy the md devices and recreate in the opposite order
# Check that the md's devnames have swapped
# Verify that the pool regains its health

log_onexit cleanup
ensure_zfsd_running


N_DEVARRAY_FILES=4
set_devs
typeset FILE0="${devarray[0]}"
typeset FILE1="${devarray[1]}"
typeset FILE2="${devarray[2]}"
typeset FILE3="${devarray[3]}"
typeset MD0=`$MDCONFIG -a -t vnode -f ${FILE0}`
[ $? -eq 0 ] || atf_fail "Failed to create md device"
typeset MD1=`$MDCONFIG -a -t vnode -f ${FILE1}`
[ $? -eq 0 ] || atf_fail "Failed to create md device"
typeset MD2=`$MDCONFIG -a -t vnode -f ${FILE2}`
[ $? -eq 0 ] || atf_fail "Failed to create md device"
typeset MD3=`$MDCONFIG -a -t vnode -f ${FILE3}`
[ $? -eq 0 ] || atf_fail "Failed to create md device"
log_must create_gnops $MD0 $MD1 $MD2 $MD3

for type in "raidz2" "mirror"; do
	# Create a pool on the supplied disks
	create_pool $TESTPOOL $type ${MD0}.nop ${MD1}.nop ${MD2}.nop ${MD3}.nop

	log_must destroy_gnop $MD0
	for ((i=0; i<5; i=i+1)); do
		$MDCONFIG -d -u $MD0 && break
		$SLEEP 1
	done
	[ -c /dev/$MD0.nop ] && atf_fail "failed to destroy $MD0"
	log_must destroy_gnop $MD1
	for ((i=0; i<5; i=i+1)); do
		$MDCONFIG -d -u $MD1 && break
		$SLEEP 1
	done
	[ -c /dev/$MD1.nop ] && atf_fail "failed to destroy $MD0"

	# Make sure that the pool is degraded
	$ZPOOL status $TESTPOOL |grep "state:" |grep DEGRADED > /dev/null
	if [ $? != 0 ]; then
		log_fail "Pool $TESTPOOL not listed as DEGRADED"
	fi

	# Do some I/O to ensure that the old vdevs will be out of date
	log_must $DD if=/dev/random of=/$TESTPOOL/randfile bs=1m count=1
	log_must $SYNC

	# Recreate the vdevs in the opposite order
	typeset MD0=`$MDCONFIG -a -t vnode -f ${FILE1}`
	[ $? -eq 0 ] || atf_fail "Failed to create md device"
	typeset MD1=`$MDCONFIG -a -t vnode -f ${FILE0}`
	[ $? -eq 0 ] || atf_fail "Failed to create md device"
	log_must create_gnops $MD0 $MD1

	wait_until_resilvered
	destroy_pool $TESTPOOL
done

log_pass
