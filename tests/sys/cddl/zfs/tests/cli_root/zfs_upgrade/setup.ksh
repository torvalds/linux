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
#

. $STF_SUITE/include/libtest.kshlib

DISK=${DISKS%% *}

# This should have been set by the .cfg script - verify it's set to something
# (we check that something later on)
if [ -z "$ZFS_VERSION" ]
then
   log_unresolved "Unable to determine ZFS Filesystem version of this machine"
else
   log_note "This machine is running ZFS Filesystem version $ZFS_VERSION"
fi

default_setup "$DISK"
