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
# ident	"@(#)setup.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/acl/acl_common.kshlib


# check svc:/network/nis/client:default state
# disable it if the state is ON
# and the state will be restored during cleanup.ksh
if [[ `$UNAME -s` != "FreeBSD" ]]; then
	log_must $RM -f $NISSTAFILE
	if [[ "ON" == $($SVCS -H -o sta svc:/network/nis/client:default) ]]; then
	    log_must $SVCADM disable -t svc:/network/nis/client:default
	    log_must $TOUCH $NISSTAFILE
	fi
fi

cleanup_user_group

# Add wheel group user
log_must add_user wheel $ZFS_ACL_ADMIN

# Create staff group and add two user to it
log_must add_group $ZFS_ACL_STAFF_GROUP
log_must add_user $ZFS_ACL_STAFF_GROUP $ZFS_ACL_STAFF1
log_must add_user $ZFS_ACL_STAFF_GROUP $ZFS_ACL_STAFF2

# Create other group and add two user to it
log_must add_group $ZFS_ACL_OTHER_GROUP
log_must add_user $ZFS_ACL_OTHER_GROUP $ZFS_ACL_OTHER1
log_must add_user $ZFS_ACL_OTHER_GROUP $ZFS_ACL_OTHER2

DISK=${DISKS%% *}
default_setup_noexit $DISK
log_must $CHMOD 777 $TESTDIR

log_pass
