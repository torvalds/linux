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
# ident	"@(#)zfs_unshare_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unshare_001_pos
#
# DESCRIPTION:
# Verify that 'zfs unshare <filesystem|mountpoint>' unshares a given shared 
# filesystem.
#
# STRATEGY:
# 1. Share filesystems
# 2. Invoke 'zfs unshare <filesystem|mountpoint>' to unshare zfs file system
# 3. Verify that the file system is unshared
# 4. Verify that unsharing an unshared file system fails
# 5. Verify that "zfs unshare -a" succeeds to unshare all zfs file systems.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-18)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	typeset -i i=0	
	while (( i < ${#mntp_fs[*]} )); do
		log_must $ZFS set sharenfs=off ${mntp_fs[((i+1))]}

		((i = i + 2))
	done

	if mounted $TESTPOOL/$TESTCLONE; then
		log_must $ZFS unmount $TESTDIR2
	fi

	[[ -d $TESTDIR2 ]] && \
		log_must $RM -rf $TESTDIR2

	if datasetexists "$TESTPOOL/$TESTCLONE"; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTCLONE
	fi

	if snapexists "$TESTPOOL/$TESTFS2@snapshot"; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS2@snapshot
	fi

	if datasetexists "$TESTPOOL/$TESTFS2"; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS2
	fi
}

#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# unshare the filesystem via <filesystem|mountpoint> argument
# and then verify it has been unshared.
#
function test_unshare # <mntp> <filesystem>
{
        typeset mntp=$1
        typeset filesystem=$2
	typeset prop_value

	prop_value=$(get_prop "sharenfs" $filesystem)

	if [[ $prop_value == "off" ]]; then
		not_shared $mntp ||
			log_must $UNSHARE -F nfs $mntp
		log_must $ZFS set sharenfs=on $filesystem
		is_shared $mntp || \
			log_fail "'$ZFS set sharenfs=on' fails to make" \
				"file system $filesystem shared." 
	fi

       	is_shared $mntp || \
		log_must $ZFS share $filesystem

        #
       	# Verify 'zfs unshare <filesystem>' works as well.
       	#
       	log_must $ZFS unshare $filesystem
	not_shared $mntp || \
		log_fail "'zfs unshare <filesystem>' fails"	

       	log_must $ZFS share $filesystem

       	log_must $ZFS unshare $mntp
 	not_shared $mntp || \
		log_fail "'zfs unshare <mountpoint>' fails"	

        log_note "Unsharing an unshared file system fails."
        log_mustnot $ZFS unshare $filesystem
	log_mustnot $ZFS unshare $mntp
}


set -A mntp_fs \
    "$TESTDIR" 	"$TESTPOOL/$TESTFS" \
    "$TESTDIR1" "$TESTPOOL/$TESTCTR/$TESTFS1" \
    "$TESTDIR2"	"$TESTPOOL/$TESTCLONE"

log_assert "Verify that 'zfs unshare [-a] <filesystem|mountpoint>' succeeds as root."
log_onexit cleanup

log_must $ZFS create $TESTPOOL/$TESTFS2
log_must $ZFS snapshot $TESTPOOL/$TESTFS2@snapshot
log_must $ZFS clone $TESTPOOL/$TESTFS2@snapshot $TESTPOOL/$TESTCLONE
log_must $ZFS set mountpoint=$TESTDIR2 $TESTPOOL/$TESTCLONE

#
# Invoke 'test_unshare' routine to test 'zfs unshare <filesystem|mountpoint>'.
#
typeset -i i=0
while (( i < ${#mntp_fs[*]} )); do
	test_unshare ${mntp_fs[i]} ${mntp_fs[((i + 1 ))]}

	((i = i + 2))
done

log_note "Verify '$ZFS unshare -a' succeds as root."

i=0
typeset sharenfs_val
while (( i < ${#mntp_fs[*]} )); do
	sharenfs_val=$(get_prop "sharenfs" ${mntp_fs[((i+1))]})
	if [[ $sharenfs_val == "on" ]]; then
        	not_shared ${mntp_fs[i]} && \
			log_must $ZFS share ${mntp_fs[((i+1))]}
	else
		log_must $ZFS set sharenfs=on ${mntp_fs[((i+1))]}
		is_shared ${mntp_fs[i]} || \
			log_fail "'$ZFS set sharenfs=on' fails to share filesystem." 
	fi

        ((i = i + 2))
done

#
# test 'zfs unshare -a '
#
log_must $ZFS unshare -a

#
# verify all shared filesystems become unshared
#
i=0
while (( i < ${#mntp_fs[*]} )); do
        not_shared ${mntp_fs[i]} || \
                log_fail "'$ZFS unshare -a' fails to unshare all shared zfs filesystems."

        ((i = i + 2))
done

log_pass "'$ZFS unshare [-a] <filesystem|mountpoint>' succeeds as root."
