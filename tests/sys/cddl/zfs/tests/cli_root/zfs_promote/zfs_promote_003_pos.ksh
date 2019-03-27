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
# ident	"@(#)zfs_promote_003_pos.ksh	1.2	07/01/09 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_promote_003_pos
#
# DESCRIPTION: 
#	'zfs promote' can deal with multi-point snapshots.
#
# STRATEGY:
#	1. Create multiple snapshots and a clone to a middle point snapshot
#	2. Promote the clone filesystem
#	3. Verify the origin filesystem and promoted filesystem include 
#	   correct datasets separated by the clone point.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-05-16)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup
{
	if snapexists ${csnap[2]}; then
		log_must $ZFS promote $fs
	fi

	typeset ds
	typeset data
	for ds in ${snap[*]}; do
		snapexists $ds && \
			log_must $ZFS destroy -rR $ds
	done
	for data in ${file[*]}; do
		[[ -e $data ]] && $RM -f $data
	done
		
}

log_assert "'zfs promote' can deal with multi-point snapshots." 
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
clone=$TESTPOOL/$TESTCLONE

# Define some arrays here to use loop to reduce code amount

# Array which stores the origin snapshots created in the origin filesystem
set -A snap "${fs}@$TESTSNAP" "${fs}@$TESTSNAP1" "${fs}@$TESTSNAP2" "${fs}@$TESTSNAP3"
# Array which stores the snapshots existing in the clone after promote operation
set -A csnap "${clone}@$TESTSNAP" "${clone}@$TESTSNAP1" "${clone}@$TESTSNAP2" \
	"${clone}@$TESTSNAP3"
# The data will inject into the origin filesystem
set -A file "$TESTDIR/$TESTFILE0" "$TESTDIR/$TESTFILE1" "$TESTDIR/$TESTFILE2" \
		"$TESTDIR/$TESTFILE3"
snapdir=$TESTDIR/$(get_snapdir_name)
# The data which will exist in the snapshot after creation of snapshot
set -A snapfile "$snapdir/$TESTSNAP/$TESTFILE0" "$snapdir/$TESTSNAP1/$TESTFILE1" \
	"$snapdir/$TESTSNAP2/$TESTFILE2" "$snapdir/$TESTSNAP3/$TESTFILE3"
csnapdir=/$clone/$(get_snapdir_name)
# The data which will exist in the snapshot of clone filesystem after promote
set -A csnapfile "${csnapdir}/$TESTSNAP/$TESTFILE0" "${csnapdir}/$TESTSNAP1/$TESTFILE1" \
	"${csnapdir}/$TESTSNAP2/$TESTFILE2"

# setup for promote testing
typeset -i i=0
while (( i < 4 )); do
	log_must $MKFILE $FILESIZE ${file[i]}
	(( i>0 )) && log_must $RM -f ${file[((i-1))]}
	log_must $ZFS snapshot ${snap[i]}

	(( i = i + 1 ))
done
log_must $ZFS clone ${snap[2]} $clone
log_must $MKFILE $FILESIZE /$clone/$CLONEFILE
log_must $RM -f /$clone/$TESTFILE2
log_must $ZFS snapshot ${csnap[3]}

log_must $ZFS promote $clone

# verify the 'promote' operation
for ds in ${snap[3]} ${csnap[*]}; do
	! snapexists $ds && \
		log_fail "The snapshot $ds disappear after zfs promote."
done
for data in ${csnapfile[*]} $TESTDIR/$TESTFILE3 /$clone/$CLONEFILE; do
	[[ ! -e $data ]] && \
		log_fail "The data file $data loses after zfs promote." 
done 

for ds in ${snap[0]} ${snap[1]} ${snap[2]}; do
	snapexists $ds && \
		log_fail "zfs promote cannot promote the snapshot $ds."
done
for data in ${snapfile[0]} ${snapfile[1]} ${snapfile[2]}; do
	[[ -e $data ]] && \
		log_fail "zfs promote cannot promote the data $data."
done

origin_prop=$(get_prop origin $fs)
[[ "$origin_prop" != "${csnap[2]}" ]] && \
	log_fail "The dependency is not correct for $fs after zfs promote."
origin_prop=$(get_prop origin $clone)
[[ "$origin_prop" != "-" ]] && \
	log_fail "The dependency is not correct for $clone after zfs promote."

log_pass "'zfs promote' deal with multi-point snapshots as expected."

