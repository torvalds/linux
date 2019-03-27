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
# ident	"@(#)zfs_send_002_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/cli_root/cli_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_send_002_pos
#
# DESCRIPTION:
#	Verify 'zfs send' can generate valid streams with a property setup. 
#
# STRATEGY:
#	1. Setup property for filesystem
#	2. Fill in some data into filesystem 
#	3. Create a full send streams 
#	4. Receive the send stream
#	5. Verify the receive result
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-11)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	snapexists $snap && \
		log_must $ZFS destroy $snap

	datasetexists $ctr && \
		log_must $ZFS destroy -r $ctr

	[[ -e $origfile ]] && \
		log_must $RM -f $origfile

	[[ -e $stream ]] && \
		log_must $RM -f $stream
}

function do_testing # <prop> <prop_value>
{
	typeset property=$1
	typeset prop_val=$2

	log_must $ZFS set $property=$prop_val $fs
	log_must $FILE_WRITE -o create -f $origfile -b $BLOCK_SIZE -c $WRITE_COUNT
	log_must $ZFS snapshot $snap
	$ZFS send $snap > $stream
	(( $? != 0 )) && \
		log_fail "'$ZFS send' fails to create send streams."
	$ZFS receive -d $ctr <$stream
	(( $? != 0 )) && \
		log_fail "'$ZFS receive' fails to receive send streams."

	#verify receive result
	! datasetexists $rstfs && \
		log_fail "'$ZFS receive' fails to restore $rstfs"
	! snapexists $rstfssnap && \
		log_fail "'$ZFS receive' fails to restore $rstfssnap"
	if [[ ! -e $rstfile ]] || [[ ! -e $rstsnapfile ]]; then
		log_fail " Data lost after receiving stream"
	fi
	
	compare_cksum $origfile $rstfile
	compare_cksum $origsnapfile $rstsnapfile

	#Destroy datasets and stream for next testing
	log_must $ZFS destroy $snap
	if is_global_zone ; then
		log_must $ZFS destroy -r $rstfs
	else
		log_must $ZFS destroy -r $ds_path
	fi
	log_must $RM -f $stream
}

log_assert "Verify 'zfs send' generates valid streams with a property setup" 
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
snap=$fs@$TESTSNAP
ctr=$TESTPOOL/$TESTCTR
if is_global_zone; then
	rstfs=$ctr/$TESTFS
else
	ds_path=$ctr/${ZONE_CTR}0
	rstfs=$ds_path/$TESTFS
fi
rstfssnap=$rstfs@$TESTSNAP
snapdir="$(get_snapdir_name)/$TESTSNAP"
origfile=$TESTDIR/$TESTFILE1
rstfile=/$rstfs/$TESTFILE1
origsnapfile=$TESTDIR/$snapdir/$TESTFILE1
rstsnapfile=/$rstfs/$snapdir/$TESTFILE1
stream=$TMPDIR/streamfile.${TESTCASE_ID}

set -A props "compression" "checksum" "recordsize"
set -A propval "on lzjb" "on fletcher2 fletcher4 sha256" \
	"512 1k 4k 8k 16k 32k 64k 128k"

#Create a dataset to receive the send stream
log_must $ZFS create $ctr

typeset -i i=0
while (( i < ${#props[*]} ))
do
	for value in ${propval[i]} 
	do
		do_testing ${props[i]} $value
	done

	(( i = i + 1 ))
done

log_pass "'zfs send' generates streams with a property setup as expected."
