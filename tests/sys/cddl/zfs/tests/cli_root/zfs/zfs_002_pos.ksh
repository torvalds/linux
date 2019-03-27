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
# ident	"@(#)zfs_002_pos.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/userquota/userquota_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_002_pos
#
# DESCRIPTION:
# With ZFS_ABORT set, all zfs commands should be able to abort and generate a core file.
#
# STRATEGY:
# 1. Create an array of zfs command
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

verify_runnable "both"

function cleanup 
{
	unset ZFS_ABORT

	if [[ -d $corepath ]]; then
		$RM -rf $corepath
	fi 
	for ds in $fs1 $fs $ctr; do
		if datasetexists $ds; then
			log_must $ZFS destroy -rRf $ds
		fi
	done
}

log_assert "With ZFS_ABORT set, all zfs commands can abort and generate a core file." 
log_onexit cleanup

#preparation work for testing
corepath=$TESTDIR/core
if [[ -d $corepath ]]; then
	$RM -rf $corepath
fi
log_must $MKDIR $corepath

ctr=$TESTPOOL/$TESTCTR
log_must $ZFS create $ctr

fs=$ctr/$TESTFS
fs1=$ctr/$TESTFS1
snap=$fs@$TESTSNAP
clone=$ctr/$TESTCLONE
streamf=$corepath/s.${TESTCASE_ID}

set -A cmds "create $fs" "list $fs" "snapshot $snap" "set snapdir=hidden $fs" \
	    "get snapdir $fs" "rollback $snap" "inherit snapdir $fs" \
	    "rename $fs $fs-new" "rename $fs-new $fs" "unmount $fs" \
	    "mount $fs" "share $fs" "unshare $fs" "send $snap \>$streamf" \
	    "receive $fs1 \<$streamf" "clone $snap $clone" "promote $clone" \
	    "promote $fs" "destroy -rRf $fs"

set -A badparams "" "create" "destroy" "snapshot" "rollback" "clone" "promote" "rename" \
		"list -*" "set" "get -*" "inherit" "mount -A" "unmount" "share" \
		"unshare" "send" "receive"

if ! is_userquota_supported; then
	typeset -i i=${cmds[#]}
	cmds[i]="allow everyone snapshot $fs"
	cmds[((i+1))]="unallow everyone snapshot $fs"

	i=${badparams[#]}
	badparams[i]="allow"
	badparams[((i+1))]="unallow"
fi


log_must $COREADM -p ${corepath}/core.%f
log_must export ZFS_ABORT=yes

for subcmd in "${cmds[@]}" "${badparams[@]}"; do
	log_mustnot $ZFS $subcmd
	corefile=${corepath}/core.zfs
	if [[ ! -e $corefile ]]; then
		log_fail "$ZFS $subcmd cannot generate core file  with ZFS_ABORT set."
	fi
	log_must $RM -f $corefile
done

log_pass "With ZFS_ABORT set, zfs command can abort and generate core file as expected."
