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
# ident	"@(#)zfs_receive_008_pos.ksh	1.3	08/02/27 SMI"
#

. $STF_SUITE/tests/cli_root/cli_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_receive_008_pos
#
# DESCRIPTION:
#	Verifying 'zfs receive -vn [<filesystem|snapshot>] 
#		   and zfs receive -vn -d <filesystem>'
#
# STRATEGY:
#	1. Fill in fs with some data
#	2. Create full and incremental send stream
#	3. run zfs receive with -v option
#	3. Dryrun zfs receive with -vn option
#	3. Dryrun zfs receive with -vn -d option
#	4. Verify receive output and result
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-14)
#
# __stc_assertion_end
#
################################################################################
function cleanup
{
	for dset in $rst_snap $rst_fs $orig_snap; do
		if datasetexists $dset; then
			log_must $ZFS destroy -fr $dset
		fi
	done

	for file in $fbackup $mnt_file $tmp_out; do
		if [[ -f $file ]]; then
			log_must $RM -f $file
		fi
	done

	if datasetexists $TESTPOOL/$TESTFS; then
		log_must $ZFS destroy -Rf $TESTPOOL/$TESTFS
		log_must $ZFS create $TESTPOOL/$TESTFS
		log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	fi
}

verify_runnable "both"
log_assert "Verifying 'zfs receive -vn [<filesystem|snapshot>] " \
		"and zfs receive -vn -d <filesystem>'"

log_onexit cleanup

typeset datasets="$TESTPOOL/$TESTFS $TESTPOOL"
typeset rst_fs=$TESTPOOL/$TESTFS/$TESTFS
typeset fbackup=$TMPDIR/fbackup.${TESTCASE_ID}
typeset tmp_out=$TMPDIR/tmpout.${TESTCASE_ID}

for orig_fs in $datasets ; do
	typeset rst_snap=$rst_fs@snap
	typeset orig_snap=$orig_fs@snap
	typeset verb_msg="receiving full stream of ${orig_snap} into ${rst_snap}"
	typeset dryrun_msg="would receive full stream of ${orig_snap} into ${rst_snap}"

	if ! datasetexists $orig_fs; then
		log_must $ZFS create $orig_fs
	fi

	typeset mntpnt
	mntpnt=$(get_prop mountpoint $orig_fs)
	if [[ $? -ne 0 ]] ; then
		log_fail "get_prop mountpoint $orig_fs failed"
	fi
	
	typeset mnt_file=$mntpnt/file1
	
	log_must $MKFILE 100m $mnt_file
	log_must $ZFS snapshot $orig_snap
	log_must eval "$ZFS send $orig_snap > $fbackup"
	
	for opt in "-v"  "-vn"; do
		if datasetexists $rst_fs; then
			log_must $ZFS destroy -fr $rst_fs
		fi
		log_note "Check ZFS receive $opt [<filesystem|snapshot>]"
		log_must eval "$ZFS receive $opt $rst_fs < $fbackup > $tmp_out 2>&1"
		if [[ $opt == "-v" ]]; then
			log_must eval "$GREP \"$verb_msg\" $tmp_out >/dev/null 2>&1"
			if ! datasetexists $rst_snap; then
				log_fail "dataset was not received, even though the"\
					" -v flag was used."
			fi		
		else
			log_must eval "$GREP \"$dryrun_msg\" $tmp_out >/dev/null 2>&1"
			if datasetexists $rst_snap; then
				log_fail "dataset was received, even though the -nv"\
					" flag was used."
			fi
		fi
	done
	
	log_note "Check ZFS receive -vn -d <filesystem>"
	if ! datasetexists $rst_fs; then
		log_must $ZFS create $rst_fs
	fi
	log_must eval "$ZFS receive -vn -d -F $rst_fs <$fbackup >$tmp_out 2>&1"
	typeset relative_path=""
	if [[ ${orig_fs} == *"/"* ]]; then
		relative_path=${orig_fs#*/}
	fi
			
	typeset leaf_fs=${rst_fs}/${relative_path}
	leaf_fs=${leaf_fs%/}
	rst_snap=${leaf_fs}@snap
	dryrun_msg="would receive full stream of ${orig_snap} into ${rst_snap}"

	log_must eval "$GREP \"$dryrun_msg\" $tmp_out > /dev/null 2>&1"
	
	if datasetexists $rst_snap; then
		log_fail "dataset $rst_snap should not existed."
	fi
	log_must $ZFS destroy -Rf $rst_fs

	cleanup
done

log_pass "zfs receive -vn [<filesystem|snapshot>] and " \
	"zfs receive -vn -d <filesystem>' succeed."
