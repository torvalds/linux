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
# ident	"@(#)setup.ksh	1.1	07/10/09 SMI"
#

. ${STF_SUITE}/include/libtest.kshlib

verify_runnable "global"

$ZPOOL set 2>&1 | $GREP version > /dev/null
if [ $? -eq 1 ]
then
	log_unsupported "zpool version property not supported on this system."
fi

DISKS_ARRAY=($DISKS)
# create a version 1 pool
log_must $ZPOOL create -f -o version=1 $TESTPOOL ${DISKS_ARRAY[0]}


# create another version 1 pool
log_must $ZPOOL create -f -o version=1 $TESTPOOL2 ${DISKS_ARRAY[1]}

log_pass
