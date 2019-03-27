#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)zvol_swap_003_pos.ksh	1.3	08/05/14 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/zvol/zvol_common.kshlib

##################################################################
#
# __stc_assertion_start
#
# ID: zvol_swap_003_pos
#
# DESCRIPTION:
# Verify that a zvol device can be used as a swap device
# through /etc/vfstab configuration.
#
# STRATEGY:
# 1. Create a pool
# 2. Create a zvol volume
# 3. Save current swaps info and delete current swaps
# 4. Modify /etc/vfstab to add entry for zvol as swap device
# 5. Use /sbin/swapadd to zvol as swap device throuth /etc/vfstab
# 6. Create a file under $TMPDIR
# 7. Verify the file
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2005-07-04)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	if [[ -f $TMPDIR/$TESTFILE ]]; then
		log_must $RM -rf $TMPDIR/$TESTFILE
	fi

	if [[ -f $TMP_VFSTAB_FILE ]]; then
		log_must $RM -rf $TMP_VFSTAB_FILE
	fi

	if [[ -f $NEW_VFSTAB_FILE ]]; then
		log_must $RM -f $NEW_VFSTAB_FILE
	fi

	if [[ -f $PREV_VFSTAB_FILE ]]; then
		log_must $MV $PREV_VFSTAB_FILE $VFSTAB_FILE
	fi

	log_must $SWAPADD $VFSTAB_FILE

        if is_swap_inuse $voldev ; then
        	log_must $SWAP -d $voldev
	fi
	
}


log_assert "Verify that a zvol device can be used as a swap device"\
	  "through /etc/vfstab configuration."

log_onexit cleanup

test_requires SWAPADD

voldev=/dev/zvol/$TESTPOOL/$TESTVOL
VFSTAB_FILE=/etc/vfstab
NEW_VFSTAB_FILE=$TMPDIR/zvol_vfstab.${TESTCASE_ID}
PREV_VFSTAB_FILE=$TMPDIR/zvol_vfstab.PREV.${TESTCASE_ID}
TMP_VFSTAB_FILE=$TMPDIR/zvol_vfstab.tmp.${TESTCASE_ID}

if [[ -f $NEW_VFSTAB_FILE ]]; then
	$RM -f $NEW_VFSTAB_FILE
fi
$TOUCH $NEW_VFSTAB_FILE
$CHMOD 777 $NEW_VFSTAB_FILE

#
# Go through each line of /etc/vfstab and
# exclude the comment line and formulate
# a new file with an entry of zvol device
# swap device.
# 

$GREP -v "^#" $VFSTAB_FILE > $TMP_VFSTAB_FILE

typeset -i fndswapline=0
while read -r i
do
	line=`$ECHO "$i" | $AWK '{print $4}'`
        if [[  $line == "swap" ]]; then
                if [[ $fndswapline -eq 0 ]]; then
                        $ECHO "$voldev"\
                              "\t-\t-\tswap\t-"\
                              "\tno\t-" \
                                >> $NEW_VFSTAB_FILE
                        fndswapline=1
                        $ECHO  "Add an entry of zvol device as"\
                                 "swap device in $VFSTAB_FILE."
                fi
        else
                $ECHO "$i" >> $NEW_VFSTAB_FILE
        fi

done < $TMP_VFSTAB_FILE

if [[ $fndswapline -eq 1 ]]; then
	log_must $CP $VFSTAB_FILE $PREV_VFSTAB_FILE 
	log_must $CP $NEW_VFSTAB_FILE $VFSTAB_FILE 
else
	log_fail  "The system has no swap device configuration in /etc/vfstab"
fi

log_note "Add zvol volume as swap space"
log_must $SWAPADD $VFSTAB_FILE

log_note "Create a file under $TMPDIR"
log_must $FILE_WRITE -o create -f $TMPDIR/$TESTFILE \
    -b $BLOCKSZ -c $NUM_WRITES -d $DATA

[[ ! -f $TMPDIR/$TESTFILE ]] &&
    log_fail "Unable to create file under $TMPDIR"

filesize=`$LS -l $TMPDIR/$TESTFILE | $AWK '{print $5}'`
tf_size=$(( BLOCKSZ * NUM_WRITES ))
(( $tf_size != $filesize )) && \
    log_fail "testfile is ($filesize bytes), expected ($tf_size bytes)"

log_pass "Successfully added a zvol to swap area through /etc/vfstab."
