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
# ident	"@(#)compress_003_pos.ksh	1.3	07/06/06 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: compress_003_pos
#
# DESCRIPTION:
# With 'compression' or 'compress'  set, changing filesystem blocksize cannot 
# cause system panic
#
# STRATEGY:
#	1. Set 'compression' or "compress" to on
#	2. Set different blocksize with ZFS filesystem
#	3. Use 'mkfile' create single block and multi-block files
#	4. Verify the system continued work
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-07-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

function cleanup 
{
	$RM -f $TESTDIR/*
}

log_assert "Changing blocksize doesn't casue system panic with compression settings"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
single_blk_file=$TESTDIR/singleblkfile.${TESTCASE_ID}
multi_blk_file=$TESTDIR/multiblkfile.${TESTCASE_ID}
typeset -i blksize=512
typeset -i fsize=0
typeset -i offset=0

for propname in "compression" "compress"
do
	for value in $(get_compress_opts zfs_compress)
	do
		log_must $ZFS set $propname=$value $fs
		if [[ $value == "gzip-6" ]]; then
			value="gzip"
		fi
		real_val=$(get_prop $propname $fs)
		[[ $real_val != $value ]] && \
			log_fail "Set property $propname=$value failed."

		(( blksize = 512 ))
		while (( blksize <= 131072 )); do
			log_must $ZFS set recordsize=$blksize $fs		
			(( offset = $RANDOM )) 
			if (( offset > blksize )); then
				(( offset = offset % blksize ))
			fi 
			if (( (offset % 2) == 0 )); then
				#keep offset as non-power-of-2
				(( offset = offset + 1 )) 
			fi
			(( fsize = offset )) 	
			log_must $MKFILE $fsize $single_blk_file
			(( fsize = blksize + offset ))
			log_must $MKFILE $fsize $multi_blk_file

			(( blksize = blksize * 2 ))
		done
	done
done

log_pass "The system works as expected while changing blocksize with compression settings"
