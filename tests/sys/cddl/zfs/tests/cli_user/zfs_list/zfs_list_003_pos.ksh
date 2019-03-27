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
# ident	"@(#)zfs_list_003_pos.ksh	1.1	07/06/05 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_list_003_pos
#
# DESCRIPTION:
# 	Verify 'zfs list -r' could recursively display any children 
#	of the dataset.
#
# STRATEGY:
# 1. Prepare a set of datasets by hierarchy.
# 2. Execute 'zfs list -r' at the top of these datasets.
# 3. Verify all child datasets are all be shown.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-05-24)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	if [[ -f $tmpfile ]]; then
		$RM -f $tmpfile
	fi
}

verify_runnable "both"
log_onexit cleanup

log_assert "Verify 'zfs list -r' could display any children recursively."

tmpfile=$TMPDIR/zfslist.out.${TESTCASE_ID}
children="$TESTPOOL/$TESTFS"

for fs in $DATASETS ; do
	children="$children $TESTPOOL/$TESTFS/$fs"
done

cd /tmp

for path in $TESTPOOL/$TESTFS $TESTDIR ./../$TESTDIR ; do
	run_unprivileged $ZFS list -rH -o name $path > $tmpfile
	for fs in $children ; do
		$GREP -q "^${fs}$" $tmpfile
		if (( $? != 0 )); then
			cat $tmpfile
			log_fail "$fs not shown in the output list."
		fi
	done
done

log_pass "'zfs list -r' could display any children recursively."
