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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)setup.ksh	1.2	07/05/29 SMI"
#

# $FreeBSD$

. ${STF_SUITE}/include/libtest.kshlib
. ${STF_SUITE}/tests/xattr/xattr.cfg

# if we're running NIS, turn it off until we clean up
# (it can cause useradd to take a long time, hitting our TIMEOUT)
$SVCS svc:/network/nis/client:default | $GREP online > /dev/null
if [ $? -eq 0 ]
then
	$SVCADM disable -t svc:/network/nis/client:default
	USES_NIS=true
fi

# Make sure we use a brand new user for this
while [ -z "${FOUND}" ]
do
	COUNT=0
	USER_EXISTS=$( $GREP $ZFS_USER /etc/passwd )
	if [ ! -z "${USER_EXISTS}" ]
	then
		ZFS_USER="${ZFS_USER}${COUNT}"
		COUNT=$(( $COUNT + 1 ))
	else
		FOUND="true"
	fi
done

log_must add_user $ZFS_GROUP $ZFS_USER

$ECHO $ZFS_USER > $TMPDIR/zfs-xattr-test-user.txt
$ECHO $USES_NIS > $TMPDIR/zfs-xattr-test-nis.txt

DISK=${DISKS%% *}
default_setup $DISK
