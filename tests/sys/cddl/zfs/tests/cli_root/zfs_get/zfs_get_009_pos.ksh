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
# ident	"@(#)zfs_get_009_pos.ksh	1.1	09/06/22 SMI"
#

. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/cli_root/zfs_get/zfs_get_list_d.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_009_pos
#
# DESCRIPTION:
#	'zfs get -d <n>' should get expected output.
#
# STRATEGY:
#	1. Create a multiple depth filesystem.
#	2. 'zfs get -d <n>' to get the output.
#	3. 'zfs get -r|egrep' to get the expected output.
#	4. Compare the two outputs, they shoud be same.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2009-05-20)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

if ! zfs_get_list_d_supported ; then
	log_unsupported "'zfs get -d' is not supported."
fi

log_assert "'zfs get -d <n>' should get expected output."
log_onexit depth_fs_cleanup

set -A all_props type used available creation volsize referenced \
	compressratio mounted origin recordsize quota reservation mountpoint \
	sharenfs checksum compression atime devices exec readonly setuid \
	snapdir aclmode aclinherit canmount primarycache secondarycache \
	usedbychildren usedbydataset usedbyrefreservation usedbysnapshots \
	userquota@root groupquota@root userused@root groupused@root

$ZFS upgrade -v > /dev/null 2>&1
if [[ $? -eq 0 ]]; then
	set -A all_props ${all_props[*]} version
fi

depth_fs_setup

mntpnt=$(get_prop mountpoint $DEPTH_FS)
DEPTH_OUTPUT="$mntpnt/depth_output"
EXPECT_OUTPUT="$mntpnt/expect_output"
typeset -i prop_numb=16
typeset -i old_val=0
typeset -i j=0
typeset eg_opt="$DEPTH_FS"$
for dp in ${depth_array[@]}; do
	(( j=old_val+1 ))
	while (( j<=dp && j<=MAX_DEPTH )); do
		eg_opt="$eg_opt""|d""$j"$
		(( j+=1 ))
	done
	for prop in $(gen_option_str "${all_props[*]}" "" "," $prop_numb); do
		log_must eval "$ZFS get -H -d $dp -o name $prop $DEPTH_FS > $DEPTH_OUTPUT"
		log_must eval "$ZFS get -rH -o name $prop $DEPTH_FS | $EGREP -e '$eg_opt' > $EXPECT_OUTPUT"
		log_must $DIFF $DEPTH_OUTPUT $EXPECT_OUTPUT
	done
	(( old_val=dp ))
done

log_pass "'zfs get -d <n>' should get expected output."

