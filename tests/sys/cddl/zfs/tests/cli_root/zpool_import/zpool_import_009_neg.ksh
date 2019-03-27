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
# ident	"@(#)zpool_import_009_neg.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

#################################################################################
# __stc_assertion_start
#
# ID: zpool_import_009_neg
#
# DESCRIPTION:
#	Try each 'zpool import' with inapplicable scenarios to make sure
#	it returns an error. include:
#		* A non-existent pool name is given
#		* '-d', but no device directory specified
#		* '-R', but no alter root directory specified
#		* '-a', but a pool name specified either
#		* more than 2 pool names is given
#		* The new pool name specified already exists
#		* Contain invalid characters not allowed in the ZFS namespace
#
# STRATEGY:
#	1. Create an array of parameters
#	2. For each parameter in the array, execute the sub-command
#	3. Verify an error is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-07)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

set -A args "blah" "-d" "-R" "-a $TESTPOOL" \
	"$TESTPOOL ${TESTPOOL}-new ${TESTPOOL}-new" \
	"$TESTPOOL $TESTPOOL1" \
	"$TESTPOOL ${TESTPOOL1}*" "$TESTPOOL ${TESTPOOL1}?"
	
set -A pools "$TESTPOOL" "$TESTPOOL1"
set -A devs "" "-d $DEVICE_DIR"

function cleanup
{
	typeset -i i=0
	typeset -i j=0

	while (( i < ${#pools[*]} )); do

		poolexists ${pools[i]} && \
			log_must $ZPOOL export ${pools[i]}

		datasetexists "${pools[i]}/$TESTFS" || \
			log_must $ZPOOL import ${devs[i]} ${pools[i]}

		ismounted "${pools[i]}/$TESTFS" || \
			log_must $ZFS mount ${pools[i]}/$TESTFS
	
		((i = i + 1))
	done

	cleanup_filesystem $TESTPOOL1 $TESTFS

        destroy_pool $TESTPOOL1
}

log_onexit cleanup

log_assert "Badly-formed 'zpool import' with inapplicable scenarios " \
	"should return an error."

setup_filesystem "$DEVICE_FILES" $TESTPOOL1 $TESTFS $TESTDIR1

log_must $ZPOOL export $TESTPOOL

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot $ZPOOL import ${args[i]}
	((i = i + 1))
done

log_pass "Badly formed 'zpool import' with inapplicable scenarios " \
	"fail as expected."
