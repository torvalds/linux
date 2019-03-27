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
# ident	"@(#)zfs_mount_006_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_root/zfs_mount/zfs_mount.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_mount_006_pos
#
# DESCRIPTION:
#	Invoke "zfs mount <filesystem>" with a filesystem
#	which mountpoint be the identical or the top of an existing one,	
#	it will fail with a return code of 1
#
# STRATEGY:
#	1. Prepare an existing mounted filesystem.
#	2. Setup a new filesystem and make sure that it is unmounted.
#       3. Mount the new filesystem using the various combinations
#		- zfs set mountpoint=<identical path> <filesystem>
#		- zfs set mountpoint=<top path> <filesystem>
#       4. Verify that mount failed with return code of 1.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-11)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	log_must force_unmount $TESTPOOL/$TESTFS

	datasetexists $TESTPOOL/$TESTFS1 && \
		cleanup_filesystem $TESTPOOL $TESTFS1

	[[ -d $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR
	log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	log_must force_unmount $TESTPOOL/$TESTFS

	return 0
}

typeset -i ret=0

log_assert "Verify that '$ZFS $mountcmd <filesystem>' " \
	"which mountpoint be the identical or the top of an existing one " \
	"will fail with return code 1."

log_onexit cleanup

unmounted $TESTPOOL/$TESTFS || \
	log_must force_unmount $TESTPOOL/$TESTFS

[[ -d $TESTDIR ]] && \
	log_must $RM -rf $TESTDIR

typeset -i MAXDEPTH=3
typeset -i depth=0
typeset mtpt=$TESTDIR

while (( depth < MAXDEPTH )); do
	mtpt=$mtpt/$depth
	(( depth = depth + 1))
done

log_must $ZFS set mountpoint=$mtpt $TESTPOOL/$TESTFS
log_must $ZFS $mountcmd $TESTPOOL/$TESTFS

mounted $TESTPOOL/$TESTFS || \
	log_unresolved "Filesystem $TESTPOOL/$TESTFS is unmounted"

log_must $ZFS create $TESTPOOL/$TESTFS1

unmounted $TESTPOOL/$TESTFS1 || \
	log_must force_unmount $TESTPOOL/$TESTFS1
	
while [[ -n $mtpt ]] ; do
	(( depth == MAXDEPTH )) && \
		log_note "Verify that '$ZFS $mountcmd <filesystem>' " \
		"which mountpoint be the identical of an existing one " \
		"will fail with return code 1."

	log_must $ZFS set mountpoint=$mtpt $TESTPOOL/$TESTFS1
	log_mustnot $ZFS $mountcmd $TESTPOOL/$TESTFS1

	unmounted $TESTPOOL/$TESTFS1 || \
		log_fail "Filesystem $TESTPOOL/$TESTFS1 is mounted."

	mtpt=${mtpt%/*}

	(( depth == MAXDEPTH )) && \
		log_note "Verify that '$ZFS $mountcmd <filesystem>' " \
		"which mountpoint be the top of an existing one " \
		"will fail with return code 1."
	(( depth = depth - 1 ))
done

log_pass "'$ZFS $mountcmd <filesystem>' " \
	"which mountpoint be the identical or the top of an existing one " \
	"will fail with return code 1."
