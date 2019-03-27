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
# ident	"@(#)compress_004_pos.ksh	1.3	07/06/06 SMI"
#
. $STF_SUITE/include/libtest.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: compress_004_pos
#
# DESCRIPTION:
# With 'compression' set, a file with non-power-of-2 blocksize storage space 
# can be freed as will normally.
#
# STRATEGY:
#	1. Set 'compression' or 'compress' to on or lzjb
#	2. Set different recordsize with ZFS filesystem
#	3. Repeatedly using 'randfree_file' to create a file and then free its  
#	   storage space with different range, the system should work normally.  
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

function create_free_testing #<file size> <file>
{
	typeset -i fsz=$1
	typeset file=$2
	typeset -i start=0
	typeset -i len=0
	typeset -i dist=0

	for start in 0 `expr $RANDOM % $fsz`
	do
		(( dist = fsz - start ))	
		for len in `expr $RANDOM % $dist` $dist \
			`expr $start + $dist`; do
			log_must $RANDFREE_FILE -l fsz -s $start \
				-n $len $file
			[[ -e $file ]] && \
				log_must $RM -f $file
		done
	done
}


log_assert "Creating non-power-of-2 blocksize file and freeing the file \
	storage space at will should work normally with compression setting" 
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
single_blk_file=$TESTDIR/singleblkfile.${TESTCASE_ID}
multi_blk_file=$TESTDIR/multiblkfile.${TESTCASE_ID}
typeset -i blksize=512
typeset -i fsize=0
typeset -i avail=0
typeset -i blknum=0

for propname in "compression" "compress"
do
	for value in $(get_compress_opts zfs_compress)
	do
		log_must $ZFS set compression=$value $fs
		real_val=$(get_prop $propname $fs)
		if [[ $value == "gzip-6" ]]; then
			value="gzip"
		fi
                [[ $real_val != $value ]] && \
                        log_fail "Set property $propname=$value failed."

		(( blksize = 512 ))
		while (( blksize <= 131072 )); do
			log_must $ZFS set recordsize=$blksize $fs		

			# doing single block testing
			(( fsize = $RANDOM )) 
			if (( fsize > blksize )); then
				(( fsize = fsize % blksize ))
			fi 
			if (( (fsize % 2) == 0 )); then
				#make sure fsize is non-power-of-2
				(( fsize = fsize + 1 ))
			fi
			create_free_testing $fsize $single_blk_file

			# doing multiple blocks testing
			avail=$(get_prop available $fs)
			(( blknum = avail / blksize ))
			# we just test <10 multi-blocks to limit testing time
                	(( blknum = blknum % 9 ))
                	while (( blknum < 2 )); do
                        	(( blknum = blknum + $RANDOM % 9 )) 
                	done
			if (( (blknum % 2) == 0 )); then
				(( blknum = blknum + 1 )) # keep blknum as odd
			fi
			(( fsize = blknum * blksize ))
			create_free_testing $fsize $multi_blk_file

			(( blksize = blksize * 2 ))
		done
	done
done

log_pass "Creating and freeing non-power-of-2 blocksize file work as expected."
