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
# ident	"@(#)zfs_unmount_008_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

###############################################################################
#
# __stc_assertion_start
#
# ID: zfs_unmount_008_neg
#
# DESCRIPTION:
# Verify that zfs unmount should fail with bad parameters or scenarios:
#	1. bad option;
#	2. too many arguments;
#	3. null arguments;
#	4. invalid datasets;
#	5. invalid mountpoint;
#	6. already unmounted zfs filesystem;
#	7. legacy mounted zfs filesystem
#
# STRATEGY:
# 1. Make an array of bad parameters
# 2. Use zfs unmount to unmount the filesystem
# 3. Verify that zfs unmount returns error
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-9)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	for ds in $vol $fs1; do
		if datasetexists $ds; then
			log_must $ZFS destroy -f $ds
		fi
	done
	
	if snapexists $snap; then
		log_must $ZFS destroy $snap
	fi
	
	if [[ -e $TMPDIR/$file ]]; then
		$RM -f $TMPDIR/$file
	fi
	if [[ -d $TMPDIR/$dir ]]; then
		$RM -rf $TMPDIR/$dir
	fi

}

log_assert "zfs unmount fails with bad parameters or scenarios"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
vol=$TESTPOOL/vol.${TESTCASE_ID}
snap=$TESTPOOL/$TESTFS@snap.${TESTCASE_ID}
set -A badargs "A" "-A" "F" "-F" "-" "-x" "-?" 

if ! ismounted $fs; then
	log_must $ZFS mount $fs
fi

log_must $ZFS snapshot $snap
if is_global_zone; then
	log_must $ZFS create -V 10m $vol
else
	vol=""
fi

# Testing bad options 
for arg in ${badargs[@]}; do
	log_mustnot eval "$ZFS unmount $arg $fs >/dev/null 2>&1" 
done 


#Testing invalid datasets
for ds in $snap $vol "blah"; do
	for opt in "" "-f"; do
		log_mustnot eval "$ZFS unmount $opt $ds >/dev/null 2>&1"
	done
done

#Testing invalid mountpoint
dir=foodir.${TESTCASE_ID}
file=foo.${TESTCASE_ID}
fs1=$TESTPOOL/fs.${TESTCASE_ID}
$MKDIR $TMPDIR/$dir
$TOUCH $TMPDIR/$file
log_must $ZFS create -o mountpoint=$TMPDIR/$dir $fs1
curpath=`$DIRNAME $0`
cd $TMPDIR
for mpt in "./$dir" "./$file"; do
	for opt in "" "-f"; do
		log_mustnot eval "$ZFS unmount $opt $mpt >/dev/null 2>&1"
	done
done
cd $curpath

#Testing null argument and too many arguments
for opt in "" "-f"; do
	log_mustnot eval "$ZFS unmount $opt >/dev/null 2>&1"
	log_mustnot eval "$ZFS unmount $opt $fs $fs1 >/dev/null 2>&1"
done

#Testing already unmounted filesystem
log_must $ZFS unmount $fs1
for opt in "" "-f"; do
	log_mustnot eval "$ZFS unmount $opt $fs1 >/dev/null 2>&1"
	log_mustnot eval "$ZFS unmount $TMPDIR/$dir >/dev/null 2>&1"
done

#Testing legacy mounted filesystem
log_must $ZFS set mountpoint=legacy $fs1
log_must $MOUNT -t zfs $fs1 $TMPDIR/$dir
for opt in "" "-f"; do
	log_mustnot eval "$ZFS unmount $opt $fs1 >/dev/null 2>&1"
done
$UMOUNT $TMPDIR/$dir

log_pass "zfs unmount fails with bad parameters or scenarios as expected."
