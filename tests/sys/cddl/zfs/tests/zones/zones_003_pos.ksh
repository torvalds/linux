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
# ident	"@(#)zones_003_pos.ksh	1.3	08/11/03 SMI"
#

. ${STF_SUITE}/include/libtest.kshlib
. ${STF_SUITE}/tests/zones/zones_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  zones_003_pos
#
# DESCRIPTION:
#
# Zone cloning via ZFS snapshots works as expected.
# We can clone zones where the zonepath is the top level of a ZFS filesystem
# using snapshots. Where the zone is not at the top level of a ZFS filesystem,
# cloning the zone uses the normal method of copying the files when
# performing the clone operation.
#
# STRATEGY:
#	1. The setup script should have created the zone.
#       2. Clone a zone-on-ZFS
#	3. Verify that ZFS snapshots were taken and used for the clone and that
#	   the new zone is indeed a clone (in the ZFS sense)
#	4. Clone a normal zone & verify that no snapshots were taken.
#	5. Clone a zone-on-ZFS, but specify the "copy" method & verify that no
#	   snapshots were taken.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-10-12)
#
# __stc_assertion_end
#
################################################################################

function cleanup {
	
	log_must $ZONEADM -z $ZONE3 uninstall -F
	log_must $ZONECFG -z $ZONE3 delete -F

	log_must $ZONEADM -z $ZONE4 uninstall -F
	log_must $ZONECFG -z $ZONE4 delete -F
}


verify_runnable "global"
log_onexit cleanup

log_assert "Zone cloning via ZFS snapshots works as expected."


# Make sure our zones exist:
if [ ! -d /$TESTPOOL/$ZONE ]
then
	log_fail "Zone dir in /$TESTPOOL/$ZONE not found!"
fi

if [ ! -d /$TESTPOOL/simple_dir/$ZONE2 ]
then
	log_fail "Zone dir /$TESTPOOL/simple_dir/$ZONE2 not found!"
fi


create_zone $ZONE3 /$TESTPOOL
create_zone $ZONE4 /$TESTPOOL/simple_dir

# Create a new zone3 based on cloning our zone
log_note "Cloning ZFS rooted zone"
log_must $ZONEADM -z $ZONE3 clone $ZONE

# Make sure our snapshot and the new filesystem is there
log_must snapexists $TESTPOOL/$ZONE@SUNWzone1
log_must datasetexists $TESTPOOL/$ZONE3

# verify that it is in fact a clone:
ORIGIN=$($ZFS get -H -o value origin $TESTPOOL/$ZONE3)
if [ "$ORIGIN" != "$TESTPOOL/$ZONE@SUNWzone1" ]
then
	log_fail "$ZONE3 does not appear to have been ZFS cloned from $ZONE"
fi

# Now uninstall that zone & the snapshot it was cloned from
log_must $ZONEADM -z $ZONE3 uninstall -F
log_must $ZONECFG -z $ZONE3 delete -F

# Again create a new zone3, but clone the non-ZFS-rooted zone2
# A snapshot should not have been created this time, but a new filesys
# should still be created.
create_zone $ZONE3 /$TESTPOOL SUNWsn1
log_note "Cloning non-ZFS rooted zone2"
log_must $ZONEADM -z $ZONE3 clone $ZONE2
log_mustnot snapexists $TESTPOOL/$ZONE2@SUNWzone1
log_must datasetexists $TESTPOOL/$ZONE3

# Finally, clone a zone using the old copy method, where
# a snapshot should not be taken.
log_note "Cloning ZFS rooted zone using copy method"
log_must $ZONEADM -z $ZONE4 clone -m copy $ZONE
log_mustnot snapexists $TESTPOOl/$ZONE@SUNWzone1

log_pass "Zone cloning via ZFS snapshots works as expected."
