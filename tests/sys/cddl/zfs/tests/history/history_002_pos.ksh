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
# ident	"@(#)history_002_pos.ksh	1.5	09/01/12 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_002_pos
#
# DESCRIPTION:
#	Create a  scenario to verify the following zfs subcommands are logged.
# 	    create, destroy, clone, rename, snapshot, rollback, 
#	    set, inherit, receive, promote.
#
# STRATEGY:
#	1. Format zpool history to file $EXPECT_HISTORY.
#	2. Invoke every sub-commands to this mirror.
#	3. Compare 'zpool history' log with expected log.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-05)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	for FileToRm in $EXPECT_HISTORY $REAL_HISTORY $tmpfile $tmpfile2; do
		[[ -f $FileToRm ]] && log_must $RM -f $FileToRm
	done
	for dataset in $fs $newfs $fsclone $vol $newvol $volclone; do
		datasetexists $dataset && $ZFS destroy -Rf $dataset
	done
	log_must $RM -rf /history.${TESTCASE_ID}
}

log_assert "Verify zfs sub-commands which modify state are logged."
log_onexit cleanup

format_history $TESTPOOL $EXPECT_HISTORY

fs=$TESTPOOL/$TESTFS1; newfs=$TESTPOOL/newfs; fsclone=$TESTPOOL/clone
vol=$TESTPOOL/$TESTVOL ; newvol=$TESTPOOL/newvol; volclone=$TESTPOOL/volclone
fssnap=$fs@fssnap; fssnap2=$fs@fssnap2
volsnap=$vol@volsnap; volsnap2=$vol@volsnap2

#	property	value		property	value
#
set -A props \
	quota		64M		recordsize	512		\
	reservation	32M		reservation	none		\
	mountpoint	/history.${TESTCASE_ID}	mountpoint	legacy		\
	mountpoint	none		sharenfs	on		\
	sharenfs	off	\
	compression	on		compression	off		\
	compression	lzjb 		aclmode		discard		\
	aclmode		groupmask	aclmode		passthrough	\
	atime		on		atime		off		\
	exec		on		exec		off		\
	setuid		on		setuid		off		\
	readonly	on		readonly	off		\
	snapdir		hidden		snapdir		visible		\
	aclinherit	discard		aclinherit	noallow		\
	aclinherit	secure		aclinherit	passthrough	\
	canmount	off		canmount	on		\
	compression	gzip		compression	gzip-$((RANDOM%9 + 1)) \
	copies		$((RANDOM%3 +1))

# Add a few extra properties not supported on FreeBSD, if applicable.  The
# currently unsupported list is in the source in libzfs_dataset.c.
if [[ $os_name != "FreeBSD" ]]; then
	set +A props \
		devices		on	devices		off		\
		zoned		on	zoned		off		\
		shareiscsi	on	shareiscsi	off		\
		xattr		on	xattr		off
fi

tmpfile=$TMPDIR/tmpfile.${TESTCASE_ID} ; tmpfile2=$TMPDIR/tmpfile2.${TESTCASE_ID}

exec_record $ZFS create $fs

typeset enc=""
enc=$(get_prop encryption $fs)
if [[ $? -ne 0 ]] || [[ -z "$enc" ]] || [[ "$enc" == "off" ]]; then
	typeset -i n=${#props[@]}

	props[$n]=checksum ;		props[((n+1))]="on"
	props[((n+2))]=checksum ;	props[((n+3))]="off"
	props[((n+4))]=checksum ;	props[((n+5))]="fletcher2"
	props[((n+6))]=checksum ;	props[((n+7))]="fletcher4"
	props[((n+8))]=checksum ;	props[((n+9))]="sha256"
fi

# Set all the property for filesystem
typeset -i i=0
while ((i < ${#props[@]})) ; do
	exec_record $ZFS set ${props[$i]}=${props[((i+1))]} $fs

	# quota, reservation, canmount can not be inherited.
	#
	if [[ ${props[$i]} != "quota" && \
	      ${props[$i]} != "reservation" && \
	      ${props[$i]} != "canmount" ]]; 
	then
		exec_record $ZFS inherit ${props[$i]} $fs
	fi

	((i += 2))
done
exec_record $ZFS create -V 64M $vol
exec_record $ZFS set volsize=32M $vol
exec_record $ZFS snapshot $fssnap
exec_record $ZFS snapshot $volsnap
exec_record $ZFS snapshot $fssnap2
exec_record $ZFS snapshot $volsnap2
log_must eval "$ZFS send -i $fssnap $fssnap2 > $tmpfile"
log_must eval "$ZFS send -i $volsnap $volsnap2 > $tmpfile2"
exec_record $ZFS destroy $fssnap2
exec_record $ZFS destroy $volsnap2
exec_record eval "$ZFS receive $fs < $tmpfile"
exec_record eval "$ZFS receive $vol < $tmpfile2"
exec_record $ZFS rollback -r $fssnap
exec_record $ZFS rollback -r $volsnap
exec_record $ZFS clone $fssnap $fsclone
exec_record $ZFS clone $volsnap $volclone
exec_record $ZFS rename $fs $newfs
exec_record $ZFS rename $vol $newvol
exec_record $ZFS promote $fsclone
exec_record $ZFS promote $volclone
exec_record $ZFS destroy $newfs
exec_record $ZFS destroy $newvol
exec_record $ZFS destroy -rf $fsclone
exec_record $ZFS destroy -rf $volclone

format_history $TESTPOOL $REAL_HISTORY

log_must $DIFF $REAL_HISTORY $EXPECT_HISTORY

log_pass "zfs sub-commands which modify state are logged passed."
