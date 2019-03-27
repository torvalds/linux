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
# ident	"@(#)zfs_inherit_003_pos.ksh	1.1	09/05/19 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_set/zfs_set_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_inherit_003_pos
#
# DESCRIPTION:
# 'zfs inherit' should return an error with bad parameters in one command. 
#
# STRATEGY:
# 1. Set an array of bad options and invlid properties to 'zfs inherit'
# 2. Execute 'zfs inherit' with bad options and passing invlid properties
# 3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup 
{
	for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL ; do
		if snapexists $ds@$TESTSNAP; then
			log_must $ZFS destroy $ds@$TESTSNAP
		fi
	done
	cleanup_user_prop $TESTPOOL
}

log_assert "'zfs inherit' should inherit user property." 
log_onexit cleanup

for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL ; do
        typeset prop_name=$(valid_user_property 10)
        typeset value=$(user_property_value 16)

	log_must eval "$ZFS set $prop_name='$value' $ds"

	log_must $ZFS snapshot $ds@$TESTSNAP

	typeset snapvalue=$(get_prop $prop_name $ds@$TESTSNAP)

	if [[ "$snapvalue" != "$value" ]] ; then
		log_fail "The '$ds@$TESTSNAP '$prop_name' value '$snapvalue' " \
			"not equal to the expected value '$value'."
	fi

	snapvalue=$(user_property_value 16) 
	log_must eval "$ZFS set $prop_name='$snapvalue' $ds@$TESTSNAP"

	log_must $ZFS inherit $prop_name $ds@$TESTSNAP

	snapvalue=$(get_prop $prop_name $ds@$TESTSNAP)

	if [[ "$snapvalue" != "$value" ]] ; then
		log_fail "The '$ds@$TESTSNAP '$prop_name' value '$snapvalue' " \
			"not equal to the expected value '$value'."
	fi


done

log_pass "'zfs inherit' inherit user property." 
