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
# ident	"@(#)cleanup.ksh	1.5	09/05/19 SMI"
#

. ${STF_SUITE}/include/libtest.kshlib

verify_runnable "both"

if ! is_global_zone ; then
	default_cleanup
fi

# Zone installs that may have been interrupted can leave the system in
# an odd state, lots of processes hanging around that will block
# our attempts to delete the zone. Remove these first.
for program in zoneadm lucreatezone lupi_zones lupi_bebasic pkginstall
do
	$PKILL -9 $program
done

# Zone installs that may have been interrupted can leave the system
# with lofs mounts from our test pool. Unmount these.
FS=$($MOUNT | $GREP $TESTPOOL | $AWK '{print $1}')

for fs in $FS
do
	$UMOUNT -f $fs
done

for zone in $ZONE $ZONE2 $ZONE3 $ZONE4 ; do
	$ZONEADM -z $zone list > /dev/null 2>&1
	if (( $? == 0 )) ; then
		$ZONEADM -z $zone halt > /dev/null 2>&1
		$ZONEADM -z $zone uninstall -F > /dev/null 2>&1
		$ZONECFG -z $zone delete -F > /dev/null 2>&1
	fi
done

default_cleanup
