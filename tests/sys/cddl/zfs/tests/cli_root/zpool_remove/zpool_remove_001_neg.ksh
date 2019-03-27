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
# ident	"@(#)zpool_remove_001_neg.ksh	1.2	08/11/03 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_remove_001_neg
#
# DESCRIPTION:
# Verify that 'zpool can not remove device except inactive hot spares from pool'
#
# STRATEGY:
# 1. Create all kinds of pool (strip, mirror, raidz, hotspare)
# 2. Try to remove device from the pool
# 3. Verify that the remove failed.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-18)
#
# __stc_assertion_end
#
################################################################################

typeset disk=${DISK}
typeset vdev_devs="${disk}p1"
typeset mirror_devs="${disk}p1 ${disk}p2"
typeset raidz_devs=${mirror_devs}
typeset raidz1_devs=${mirror_devs}
typeset raidz2_devs="${mirror_devs} ${disk}p3"
typeset spare_devs1="${disk}p1"
typeset spare_devs2="${disk}p2"

function check_remove
{
        typeset pool=$1
        typeset devs="$2"
        typeset dev

        for dev in $devs; do
                log_mustnot $ZPOOL remove $dev
        done

        destroy_pool $pool

}

function cleanup
{
        if poolexists $TESTPOOL; then
                destroy_pool $TESTPOOL
        fi
}

set -A create_args "$vdev_devs" "mirror $mirror_devs"  \
		"raidz $raidz_devs" "raidz $raidz1_devs" \
		"raidz2 $raidz2_devs" \
		"$spare_devs1 spare $spare_devs2"
		
set -A verify_disks "$vdev_devs" "$mirror_devs" "$raidz_devs" \
		"$raidz1_devs" "$raidz2_devs" "$spare_devs1"
		

log_assert "Check zpool remove <pool> <device> can not remove " \
	"active device from pool"
	
log_onexit cleanup

typeset -i i=0
while [[ $i -lt ${#create_args[*]} ]]; do
	log_must $ZPOOL create $TESTPOOL ${create_args[i]}
	check_remove $TESTPOOL "${verify_disks[i]}"
	(( i = i + 1))
done

log_pass "'zpool remove <pool> <device> fail as expected .'"
