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
# ident	"@(#)cleanup.ksh	1.4	08/11/03 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_copies/zfs_copies.kshlib
. $STF_SUITE/tests/cli_root/zfs_copies/zfs_copies.cfg

if ! fs_prop_exist "copies" ; then
	log_unsupported "copies is not supported by this release."
fi

#
# umount the ufs fs if there is timedout in the ufs test
#

if ismounted $UFS_MNTPOINT ufs ; then
	log_must $UMOUNT -f $UFS_MNTPOINT
	$RM -fr $UFS_MNTPOINT
fi

default_cleanup
