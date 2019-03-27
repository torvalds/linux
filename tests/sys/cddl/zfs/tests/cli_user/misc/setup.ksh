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
# ident	"@(#)setup.ksh	1.2	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib

# This setup script is moderately complex, as it creates scenarios for all
# of the tests included in this directory. Usually we'd want each test case
# to setup/teardown it's own configuration, but this would be time consuming
# given the nature of these tests. However, as a side-effect, one test
# leaving the system in an unknown state could impact other test cases.


DISK=${DISKS%% *}
VOLSIZE=150m
TESTVOL=testvol

# Create a default setup that includes a volume
default_setup_noexit "$DISK" "" "volume"

#
# The rest of this setup script creates a ZFS filesystem configuration
# that is used to test the rest of the zfs subcommands in this directory.
#

# create a snapshot and a clone to test clone promote
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snap
log_must $ZFS clone $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS/clone
# create a file in the filesystem that isn't in the above snapshot
$TOUCH /$TESTDIR/file.txt


# create a non-default property and a child we can use to test inherit
log_must $ZFS create $TESTPOOL/$TESTFS/$TESTFS2
log_must $ZFS set snapdir=hidden $TESTPOOL/$TESTFS


# create an unmounted filesystem to test unmount
log_must $ZFS create $TESTPOOL/$TESTFS/$TESTFS2.unmounted
log_must $ZFS unmount $TESTPOOL/$TESTFS/$TESTFS2.unmounted


# send our snapshot to a known file in $TMPDIR
$ZFS send $TESTPOOL/$TESTFS@snap > $TMPDIR/zfstest_datastream.dat
if [ ! -s $TMPDIR/zfstest_datastream.dat ]
then
	log_fail "Zfs send datafile was not created!"
fi
log_must $CHMOD 644 $TMPDIR/zfstest_datastream.dat


# create a filesystem that has particular properties to test set/get
log_must $ZFS create -o version=1 $TESTPOOL/$TESTFS/prop
set -A props $PROP_NAMES
set -A prop_vals $PROP_VALS
typeset -i i=0

while [[ $i -lt ${#props[*]} ]]
do
        prop_name=${props[$i]}
	prop_val=${prop_vals[$i]}
	log_must $ZFS set $prop_name=$prop_val $TESTPOOL/$TESTFS/prop
        i=$(( $i + 1 ))
done


# create a filesystem we don't mind renaming
log_must $ZFS create $TESTPOOL/$TESTFS/renameme


if is_global_zone
then
	# create a filesystem we can share
	log_must $ZFS create $TESTPOOL/$TESTFS/unshared
	log_must $ZFS set sharenfs=off $TESTPOOL/$TESTFS/unshared
	
	# create a filesystem that we can unshare
	log_must $ZFS create $TESTPOOL/$TESTFS/shared
	log_must $ZFS set sharenfs=on $TESTPOOL/$TESTFS/shared
fi


# check for upgrade support
$ZFS upgrade > /dev/null 2>&1
HAS_UPGRADE=$?

if  [ $HAS_UPGRADE -eq 0 ]
then
	log_must $ZFS create -o version=1 $TESTPOOL/$TESTFS/version1
fi

$ZFS 2>&1 | $GREP "allow" > /dev/null
if (( $? == 0 )); then
	log_must $ZFS create -o version=1 $TESTPOOL/$TESTFS/allowed
	log_must $ZFS allow everyone create $TESTPOOL/$TESTFS/allowed
fi

if is_global_zone; then
	# Now create several virtual disks to test zpool with
	log_must create_vdevs \
		/$TESTDIR/disk1.dat \
		/$TESTDIR/disk2.dat \
		/$TESTDIR/disk3.dat \
		/$TESTDIR/disk-additional.dat \
		/$TESTDIR/disk-export.dat \
		/$TESTDIR/disk-offline.dat \
		/$TESTDIR/disk-spare1.dat \
		/$TESTDIR/disk-spare2.dat

	# and create a pool we can perform attach remove replace,
	# etc. operations with
	log_must $ZPOOL create $TESTPOOL.virt mirror /$TESTDIR/disk1.dat \
	 /$TESTDIR/disk2.dat /$TESTDIR/disk3.dat /$TESTDIR/disk-offline.dat \
	 spare /$TESTDIR/disk-spare1.dat

	# Offline one of the disks to test online
	log_must $ZPOOL offline $TESTPOOL.virt /$TESTDIR/disk-offline.dat

	# create an exported pool to test import
	log_must $ZPOOL create $TESTPOOL.exported /$TESTDIR/disk-export.dat
	log_must $ZPOOL export $TESTPOOL.exported

	# Now setup pool properties if they're supported
	GET=$($ZPOOL 2>&1 | $FGREP "get <all")
	if [ -n "$GET" ]
	then
		set -A props $POOL_PROPS
		set -A prop_vals $POOL_VALS
		typeset -i i=0
	
		while [[ $i -lt ${#props[*]} ]]
		do
	        	prop_name=${props[$i]}
			prop_val=${prop_vals[$i]}
			log_must $ZPOOL set $prop_name=$prop_val $TESTPOOL
	        	i=$(( $i + 1 ))
		done
	fi

	# copy a v1 pool from cli_root
	$CP $STF_SUITE/tests/cli_root/zpool_upgrade/blockfiles/zfs-pool-v1.dat.Z \
	 /$TESTDIR
	log_must $UNCOMPRESS /$TESTDIR/zfs-pool-v1.dat.Z
	log_must $ZPOOL import -d /$TESTDIR v1-pool
fi
log_pass
