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
# ident	"@(#)zones_001_pos.ksh	1.3	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  zones_001_pos
#
# DESCRIPTION:
#
# The zone created by the default zones setup should have ZFS zvols,
# datasets and filesystems present.
#
# STRATEGY:
#	1. For each ZFS object type
#       2. Perform a basic sanity check for that object in the local zone.
#	3. Check that the top level dataset is read only.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-10-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "local"


log_assert "Local zone contains ZFS datasets as expected."


# check to see if our zvol exists:
if [ ! -b /dev/zvol/zonepool/zone_zvol ]
then
    log_fail "block device /dev/zvol/zonepool/zone_zvol not found!"
fi

if [ ! -c /dev/zvol/zonepool/zone_zvol ]
then
    log_fail "char device /dev/zvol/zonepool/zone_zvol not found!"
fi

# check to see if the device appears sane - create a UFS filesystem on it.
$ECHO y | $NEWFS /dev/zvol/zonepool/zone_zvol > /dev/null

if [ $? -ne 0 ]
then
    log_fail "Failed to create UFS filesystem on a zvol in the zone!"
fi

$MKDIR /ufs.${TESTCASE_ID}
log_must $MOUNT /dev/zvol/zonepool/zone_zvol /ufs.${TESTCASE_ID}
log_must $UMOUNT /ufs.${TESTCASE_ID}
$RM -rf /ufs.${TESTCASE_ID}


# Next check to see if the datasets exist as expected.
for dataset in 0 1 2 3 4
do
    DATASET=zonepool/zonectr${dataset}
    if [ ! -d /${DATASET} ]
    then
        log_note "Missing zone dataset ${DATASET}!"
    fi
    log_must $ZFS create ${DATASET}/fs
    if [ ! -d /${DATASET}/fs ]
    then
        log_fail "ZFS create failed to create child dataset of ${DATASET}"
    fi
    log_must $ZFS destroy ${DATASET}/fs
done

# Next check to see that the root dataset is readonly
log_mustnot $ZFS create zonepool/fs
log_mustnot $ZFS mount zonepool

log_pass "Local zone contains ZFS datasets as expected."

