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
#ident	"@(#)zpool_create_011_neg.ksh	1.5	08/11/03 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_create/zpool_create.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_create_011_neg
#
# DESCRIPTION:
# 'zpool create' will fail in the following cases:
# existent pool; device is part of an active pool; nested virtual devices;
# differently sized devices without -f option; device being currently
# mounted; devices in /etc/vfstab; specified as the dedicated dump device.
#
# STRATEGY:
# 1. Create case scenarios
# 2. For each scenario, try to create a new pool with the virtual devices
# 3. Verify the creation is failed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
        for pool in $TESTPOOL $TESTPOOL1
        do
                destroy_pool $pool
        done

	if [[ -n $saved_dump_dev ]]; then
		log_must $DUMPADM -u -d $saved_dump_dev
	fi
}

log_assert "'zpool create' should be failed with inapplicable scenarios."
log_onexit cleanup

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi
pooldev1=${disk}p1
pooldev2=${disk}p2
mirror1="${disk}p2 ${disk}p3"
mirror2="${disk}p4 ${disk}p5"
raidz1=$mirror1
raidz2=$mirror2
diff_size_dev="${disk}p6 ${disk}p7"
vfstab_dev=$(find_vfstab_dev)
specified_dump_dev=${disk}p1
saved_dump_dev=$(save_dump_dev)

lba=$(get_partition_end $disk 6)
set_partition 7 "$lba" $SIZE1 $disk
create_pool "$TESTPOOL" "$pooldev1"

#
# Set up the testing scenarios parameters
#
set -A arg "$TESTPOOL $pooldev2" \
        "$TESTPOOL1 $pooldev1" \
        "$TESTPOOL1 $TESTDIR0/$FILEDISK0" \
        "$TESTPOOL1 mirror mirror $mirror1 mirror $mirror2" \
        "$TESTPOOL1 raidz raidz $raidz1 raidz $raidz2" \
        "$TESTPOOL1 raidz1 raidz1 $raidz1 raidz1 $raidz2" \
        "$TESTPOOL1 mirror raidz $raidz1 raidz $raidz2" \
        "$TESTPOOL1 mirror raidz1 $raidz1 raidz1 $raidz2" \
        "$TESTPOOL1 raidz mirror $mirror1 mirror $mirror2" \
        "$TESTPOOL1 raidz1 mirror $mirror1 mirror $mirror2" \
        "$TESTPOOL1 mirror $diff_size_dev" \
        "$TESTPOOL1 raidz $diff_size_dev" \
        "$TESTPOOL1 raidz1 $diff_size_dev" \
	"$TESTPOOL1 mirror $mirror1 spare $mirror2 spare $diff_size_dev" \
        "$TESTPOOL1 $vfstab_dev" \
        "$TESTPOOL1 ${disk}s10" \
	"$TESTPOOL1 spare $pooldev2"

typeset -i i=0
while (( i < ${#arg[*]} )); do
        log_mustnot $ZPOOL create ${arg[i]}
        (( i = i+1 ))
done

# now destroy the pool to be polite
log_must $ZPOOL destroy -f $TESTPOOL

# create/destroy a pool as a simple way to set the partitioning
# back to something normal so we can use this $disk as a dump device
log_must $ZPOOL create -f $TESTPOOL3 $disk
log_must $ZPOOL destroy -f $TESTPOOL3

log_must $DUMPADM -d /dev/$specified_dump_dev
log_mustnot $ZPOOL create -f $TESTPOOL1 "$specified_dump_dev"

# Also check to see that in-use checking prevents us from creating
# a zpool from just the first slice on the disk.
log_mustnot $ZPOOL create -f $TESTPOOL1 ${specified_dump_dev}s0

log_pass "'zpool create' is failed as expected with inapplicable scenarios."
