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
# ident	"@(#)zfs_allow_001_pos.ksh	1.3	08/11/03 SMI"
#

. $STF_SUITE/tests/delegate/delegate_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_allow_001_pos
#
# DESCRIPTION:
# 	"everyone" is interpreted as the keyword "everyone" whatever the same
# 	name user or group is existing.
#
# STRATEGY:
#	1. Create user 'everyone'.
#	2. Verify 'everyone' is interpreted as keywords.
#	3. Create group 'everyone'.
#	4. Verify 'everyone' is interpreted as keywords.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-09-14)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if [[ $user_added == "TRUE" ]] ; then
		del_user everyone
	fi
	if [[ $group_added == "TRUE" ]] ; then
		del_group everyone
	fi

	restore_root_datasets
}

log_assert "everyone' is interpreted as a keyword even if a user " \
	"or group named 'everyone' exists."
log_onexit cleanup

eval set -A dataset $DATASETS
enc=$(get_prop encryption $dataset)
if [[ $? -eq 0 ]] && [[ -n "$enc" ]] && [[ "$enc" != "off" ]]; then
	typeset perms="snapshot,reservation,compression,send,allow,\
userprop"
else
	typeset perms="snapshot,reservation,compression,checksum,\
send,allow,userprop"
fi

log_note "Create a user called 'everyone'."
if ! $ID everyone > /dev/null 2>&1; then
	user_added="TRUE"
	log_must $USERADD everyone
fi
for dtst in $DATASETS ; do
	log_must $ZFS allow everyone $perms $dtst
	log_must verify_perm $dtst $perms $EVERYONE "everyone"
done
log_must restore_root_datasets
if [[ $user_added == "TRUE" ]]; then
	log_must $USERDEL everyone
fi

log_note "Created a group called 'everyone'."
if ! $CAT /etc/group | $AWK -F: '{print $1}' | \
	$GREP -w 'everyone' > /dev/null 2>&1
then
	group_added="TRUE"
	log_must $GROUPADD everyone
fi

for dtst in $DATASETS ; do
	log_must $ZFS allow everyone $perms $dtst
	log_must verify_perm $dtst $perms $EVERYONE
done
log_must restore_root_datasets
if [[ $group_added == "TRUE" ]]; then
	log_must $GROUPDEL everyone
fi

log_pass "everyone is always interpreted as keyword passed."
