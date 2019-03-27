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
# ident	"@(#)zones_004_pos.ksh	1.3	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  zones_004_pos
#
# DESCRIPTION:
#
# Deleting a zone, where the zonepath parent dir is the top level of a ZFS
# file system, causes that underlying filesystem to be deleted. Deleting
# the non-ZFS zone does not delete any filesystems.
#
# STRATEGY:
#	1. The setup script should have created the zone.
#       2. Delete our ZFS rooted zone, verify the filesystem has been deleted.
#	3. Delete our non-ZFS rooted zone, the zonepath dir should still exist.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-10-11)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "A ZFS fs is destroyed when the zone it was created for is deleted."

# Make sure our zones exist:
if [ ! -d /$TESTPOOL/$ZONE ]
then
	log_fail "Zone dir in /$TESTPOOL/$ZONE not found!"
fi

if [ ! -d /$TESTPOOL/simple_dir/$ZONE2 ]
then
	log_fail "Zone dir /$TESTPOOL/simple_dir/$ZONE2 not found!"
fi


# delete our ZFS rooted zone
log_must $ZONEADM -z $ZONE uninstall -F
log_must $ZONECFG -z $ZONE delete -F
log_mustnot eval "$ZFS list $TESTPOOL/$ZONE > /dev/null 2>&1"

# delete our non-ZFS rooted zone
log_must $ZONEADM -z $ZONE2 uninstall -F
log_must $ZONECFG -z $ZONE2 delete -F
if [ ! -d /$TESTPOOL/simple_dir ]
then
	log_fail "On deleting $ZONE2, the dir above zonepath was destroyed!"
fi


log_pass "A ZFS fs is destroyed when the zone it was created for is deleted."
