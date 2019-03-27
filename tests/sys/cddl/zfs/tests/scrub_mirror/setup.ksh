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
# ident	"@(#)setup.ksh	1.5	09/01/12 SMI"
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.kshlib

verify_runnable "global"

if [[ -n $SINGLE_DISK ]]; then
	log_note "Partitioning a single disk ($SINGLE_DISK)"
else
	log_note "Partitioning disks ($MIRROR_PRIMARY $MIRROR_SECONDARY)"
fi
wipe_partition_table $SINGLE_DISK $MIRROR_PRIMARY $MIRROR_SECONDARY
log_must set_partition ${SIDE_PRIMARY##*p} "" $MIRROR_SIZE $MIRROR_PRIMARY
log_must set_partition ${SIDE_SECONDARY##*p} "" $MIRROR_SIZE $MIRROR_SECONDARY

default_mirror_setup $SIDE_PRIMARY $SIDE_SECONDARY

log_pass
