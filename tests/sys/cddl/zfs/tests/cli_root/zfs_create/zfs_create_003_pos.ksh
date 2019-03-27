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
# ident	"@(#)zfs_create_003_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_create_003_pos
#
# DESCRIPTION:
# 'zfs create [-b <blocksize>] -V <size> <volume>' can create a volume 
# with specified blocksize, which is power of 2 between 512 - 128k.
#
# STRATEGY:
# 1. Create a volume with blocksize in the storage pool
# 2. Verify the volume created successfully
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-11-23)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $vol && \
		log_must $ZFS destroy -f $vol
}

log_assert "Verify creating volume with specified blocksize works."
log_onexit cleanup

set -A options "" "-b 1k" "-b 1K" "-b 1024" "-b 1024b"
vol=$TESTPOOL/$TESTVOL

typeset -i i=0
while (( i < ${#options[*]} )); do 
	log_must $ZFS create ${options[i]} -V $VOLSIZE $vol
	datasetexists $vol || \
		log_fail "zfs create ${options[i]} -V $VOLSIZE $vol fail."

	log_must $ZFS destroy -f $vol
	((i = i + 1))
done

log_pass "'zfs create [-b <blocksize>] -V <size> <volume>' works as expected."
