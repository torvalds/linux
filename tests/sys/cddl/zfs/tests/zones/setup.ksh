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
# ident	"@(#)setup.ksh	1.3	09/05/19 SMI"
#

. ${STF_SUITE}/include/libtest.kshlib
. ${STF_SUITE}/tests/zones/zones_common.kshlib

verify_runnable "both"
check_version "5.11"
if [ $? -eq 1 ]
then
	log_unsupported "ZFS zone clone tests unsupported on this release."
fi

if ! is_global_zone ; then
	log_pass
fi

DISK=${DISKS%% *}
default_setup_noexit $DISK

# create a zone on ZFS
create_zone $ZONE /$TESTPOOL
install_zone $ZONE

log_must $MKDIR -p -m 0700 /$TESTPOOL/simple_dir

# create a normal zone - again on ZFS, but with the zonepath
# being a simple directory, rather than a top-level filesystem.
# We also create this as a branded zone.
create_zone $ZONE2 /$TESTPOOL/simple_dir SUNWsn1
install_zone $ZONE2

# Now make sure those zones are visible
log_must eval "$ZONEADM -z $ZONE list > /dev/null 2>&1"
log_must eval "$ZONEADM -z $ZONE2 list > /dev/null 2>&1"

log_pass "Setup created zones $ZONE and $ZONE2"
