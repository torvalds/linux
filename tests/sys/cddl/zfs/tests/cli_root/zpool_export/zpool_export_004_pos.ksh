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
# ident	"@(#)zpool_export_004_pos.ksh	1.2	09/06/22 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zpool_export_004_pos 
#
# DESCRIPTION:
#	Verify zpool export succeed or fail with spare.
#
# STRATEGY:
#	1. Create two mirror pools with same spare.
#	2. Verify zpool export one pool succeed.
#	3. Import the pool.
#	4. Replace one device with the spare and detach it in one pool.
#	5. Verify zpool export the pool succeed.
#	6. Import the pool.
#	7. Replace one device with the spare in one pool.
#	8. Verify zpool export the pool fail.
#	9. Verify zpool export the pool with "-f" succeed.
#	10. Import the pool.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-03-10)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	restart_zfsd

	mntpnt=$(get_prop mountpoint $TESTPOOL)
        datasetexists $TESTPOOL1 || log_must $ZPOOL import -d $mntpnt $TESTPOOL1
	datasetexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	datasetexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	typeset -i i=0
	while ((i < 5)); do
		if [[ -e $mntpnt/vdev$i ]]; then 
			log_must $RM -f $mntpnt/vdev$i
		fi
		((i += 1))
	done
}


log_assert "Verify zpool export succeed or fail with spare."
log_onexit cleanup
# Stop ZFSD because it interferes with our manually activated spares
stop_zfsd

mntpnt=$(get_prop mountpoint $TESTPOOL)

typeset -i i=0
while ((i < 5)); do
	log_must create_vdevs $mntpnt/vdev$i
	eval vdev$i=$mntpnt/vdev$i
	((i += 1))
done

log_must $ZPOOL create $TESTPOOL1 mirror $vdev0 $vdev1 spare $vdev4
log_must $ZPOOL create $TESTPOOL2 mirror $vdev2 $vdev3 spare $vdev4

log_must $ZPOOL export $TESTPOOL1
log_must $ZPOOL import -d $mntpnt $TESTPOOL1

log_must $ZPOOL replace $TESTPOOL1 $vdev0 $vdev4
log_must $ZPOOL detach $TESTPOOL1 $vdev4
log_must $ZPOOL export $TESTPOOL1
log_must $ZPOOL import -d $mntpnt $TESTPOOL1

log_must $ZPOOL replace $TESTPOOL1 $vdev0 $vdev4
log_mustnot $ZPOOL export $TESTPOOL1

log_must $ZPOOL export -f $TESTPOOL1
log_must $ZPOOL import -d $mntpnt  $TESTPOOL1

log_pass "Verify zpool export succeed or fail with spare."

