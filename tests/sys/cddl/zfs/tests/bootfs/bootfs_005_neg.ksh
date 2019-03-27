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
# ident	"@(#)bootfs_005_neg.ksh	1.2	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zpool_upgrade/zpool_upgrade.cfg
. $STF_SUITE/tests/cli_root/zpool_upgrade/zpool_upgrade.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  bootfs_005_neg
#
# DESCRIPTION:
#
# Boot properties cannot be set on pools with older versions
#
# STRATEGY:
# 1. Copy and import some pools of older versions
# 2. Create a filesystem on each
# 3. Verify that zpool set bootfs fails on each
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-03-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup {

	#
	# we need destroy pools that created on top of $TESTPOOL first
	#
	typeset pool_name
	for config in $CONFIGS; do
		pool_name=$($ENV| $GREP "ZPOOL_VERSION_${config}_NAME"\
                	| $AWK -F= '{print $2}')
		if poolexists $pool_name; then
			log_must $ZPOOL destroy -f $pool_name
		fi
	done
	destroy_pool $TESTPOOL
}

$ZPOOL set 2>&1 | $GREP bootfs > /dev/null
if [ $? -ne 0 ]
then
        log_unsupported "bootfs pool property not supported on this release."
fi

log_assert "Boot properties cannot be set on pools with older versions"

# These are configs from zpool_upgrade.cfg - see that file for more info.
CONFIGS="1 2 3"

log_onexit cleanup
log_must $ZPOOL create -f $TESTPOOL $DISKS

for config in $CONFIGS
do
	create_old_pool $config
	POOL_NAME=$($ENV| $GREP "ZPOOL_VERSION_${config}_NAME"\
		| $AWK -F= '{print $2}')
	log_must $ZFS create $POOL_NAME/$FS
	log_mustnot $ZPOOL set bootfs=$POOL_NAME/$FS $POOL_NAME
	log_must destroy_upgraded_pool $config
done

log_pass "Boot properties cannot be set on pools with older versions"
