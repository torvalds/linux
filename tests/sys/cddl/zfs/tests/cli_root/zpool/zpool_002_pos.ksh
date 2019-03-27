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
# ident	"@(#)zpool_002_pos.ksh	1.2	09/01/12 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_002_pos
#
# DESCRIPTION:
# With ZFS_ABORT set, all zpool commands should be able to abort and generate a core file.
#
# STRATEGY:
# 1. Create an array of zpool command
# 2. Execute each command in the array
# 3. Verify the command aborts and generate a core file 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-29)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup 
{
	unset ZFS_ABORT

	if [[ -d $corepath ]]; then
		$RM -rf $corepath
	fi 
	if poolexists $pool; then
		log_must $ZPOOL destroy -f $pool
	fi
}

log_assert "With ZFS_ABORT set, all zpool commands can abort and generate a core file." 
log_onexit cleanup

#preparation work for testing
corepath=$TESTDIR/core
if [[ -d $corepath ]]; then
	$RM -rf $corepath
fi
$MKDIR $corepath

pool=pool.${TESTCASE_ID}
vdev1=$TESTDIR/file1
vdev2=$TESTDIR/file2
vdev3=$TESTDIR/file3
log_must create_vdevs $vdev1 $vdev2 $vdev3

set -A cmds "create $pool mirror $vdev1 $vdev2" "list $pool" "iostat $pool" \
	"status $pool" "upgrade $pool" "get delegation $pool" "set delegation=off $pool" \
	"export $pool" "import -d $TESTDIR $pool" "offline $pool $vdev1" \
	"online $pool $vdev1" "clear $pool" "detach $pool $vdev2" \
	"attach $pool $vdev1 $vdev2" "replace $pool $vdev2 $vdev3" \
	"scrub $pool" "destroy -f $pool"
	  
set -A badparams "" "create" "destroy" "add" "remove" "list *" "iostat" "status" \
		"online" "offline" "clear" "attach" "detach" "replace" "scrub" \
		"import" "export" "upgrade" "history -?" "get" "set" 

$COREADM -p ${corepath}/core.%f
export ZFS_ABORT=yes

for subcmd in "${cmds[@]}" "${badparams[@]}"; do
	$ZPOOL $subcmd >/dev/null 2>&1 
	corefile=${corepath}/core.zpool
	if [[ ! -e $corefile ]]; then
		log_fail "$ZPOOL $subcmd cannot generate core file  with ZFS_ABORT set."
	fi
	$RM -f $corefile
done

log_pass "With ZFS_ABORT set, zpool command can abort and generate core file as expected."
