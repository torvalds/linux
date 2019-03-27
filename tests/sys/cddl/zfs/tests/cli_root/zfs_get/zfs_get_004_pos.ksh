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
# ident	"@(#)zfs_get_004_pos.ksh	1.3	07/02/06 SMI"
#

. $STF_SUITE/include/libtest.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_get_004_pos
#
# DESCRIPTION:
# Verify 'zfs get all' can get all properties for all datasets in the system
#
# STRATEGY:
#	1. Create datasets for testing 
#	2. Issue 'zfs get all' command
#	3. Verify the command gets all available properties of all datasets 
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-08-31)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

#check 'zfs get all' supportability with the installed OS version
$ZFS get all >/dev/null 2>&1
(( $? != 0 )) && log_unsupported "ZFS get all option is unsupported."


function cleanup
{
	[[ -e $propfile ]] && $RM -f $propfile
	
	datasetexists $clone  && \
		log_must $ZFS destroy $clone
	for snap in $fssnap $volsnap ; do
		snapexists $snap && \
			log_must $ZFS destroy $snap
	done

	if [[ -n $globalzone ]] ; then
		for pool in $TESTPOOL1 $TESTPOOL2 $TESTPOOL3; do
			poolexists $pool && \
				log_must $ZPOOL destroy -f $pool
		done
		for file in `$LS $TESTDIR1/poolfile*`; do
			$RM -f $file
		done
	else
		for fs in $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS2 $TESTPOOL/$TESTFS3; do
			datasetexists $fs && \
				log_must $ZFS destroy -rf $fs
		done
	fi
}

log_assert "Verify the functions of 'zfs get all' work."
log_onexit cleanup

typeset globalzone=""

if is_global_zone ; then
	globalzone="true"
fi

set -A opts "" "-r" "-H" "-p" "-rHp" "-o name" \
	"-s local,default,temporary,inherited,none" \
	"-o name -s local,default,temporary,inherited,none" \
	"-rHp -o name -s local,default,temporary,inherited,none" 
set -A usrprops "a:b=c" "d_1:1_e=0f" "123:456=789"

fs=$TESTPOOL/$TESTFS
fssnap=$fs@$TESTSNAP
clone=$TESTPOOL/$TESTCLONE
volsnap=$TESTPOOL/$TESTVOL@$TESTSNAP

#set user defined properties for $TESTPOOL
for usrprop in ${usrprops[@]}; do
	log_must $ZFS set $usrprop $TESTPOOL
done
# create snapshot and clone in $TESTPOOL
log_must $ZFS snapshot $fssnap
log_must $ZFS clone $fssnap $clone
log_must $ZFS snapshot $volsnap

# collect datasets which can be set user defined properties
usrpropds="$clone $fs"

# collect all datasets which we are creating
allds="$fs $clone $fssnap $volsnap"

#create pool and datasets to guarantee testing under multiple pools and datasets.
file=$TESTDIR1/poolfile
typeset -i FILESIZE=104857600    #100M
(( DFILESIZE = FILESIZE * 2 ))   # double of FILESIZE
typeset -i VOLSIZE=10485760      #10M
availspace=$(get_prop available $TESTPOOL)
typeset -i i=0

# make sure 'availspace' is larger then twice of FILESIZE to create a new pool.
# If any, we only totally create 3 pools for multple datasets testing to limit
# testing time
while (( availspace > DFILESIZE )) && (( i < 3 )) ; do  
	(( i += 1 ))

	if [[ -n $globalzone ]] ; then
		log_must create_vdevs ${file}$i
		eval pool=\$TESTPOOL$i
		log_must $ZPOOL create $pool ${file}$i
	else
		eval pool=$TESTPOOL/\$TESTFS$i
		log_must $ZFS create $pool
	fi

	#set user defined properties for testing
	for usrprop in ${usrprops[@]}; do
		log_must $ZFS set $usrprop $pool
	done
	
	#create datasets in pool
	log_must $ZFS create $pool/$TESTFS
	log_must $ZFS snapshot $pool/$TESTFS@$TESTSNAP
	log_must $ZFS clone $pool/$TESTFS@$TESTSNAP $pool/$TESTCLONE

	if [[ -n $globalzone ]] ; then
		log_must $ZFS create -V $VOLSIZE $pool/$TESTVOL
	else
		log_must $ZFS create $pool/$TESTVOL
	fi

	ds=`$ZFS list -H -r -o name -t filesystem,volume $pool`
	usrpropds="$usrpropds $pool/$TESTFS $pool/$TESTCLONE $pool/$TESTVOL"
	allds="$allds $pool/$TESTFS $pool/$TESTCLONE $pool/$TESTVOL \
		$pool/$TESTFS@$TESTSNAP"

	availspace=$(get_prop available $TESTPOOL)
done

#the expected number of property for each type of dataset in this testing
typeset -i fspropnum=27
typeset -i snappropnum=8
typeset -i volpropnum=15
propfile=$TMPDIR/allpropfile.${TESTCASE_ID}

typeset -i i=0
typeset -i propnum=0
typeset -i failflag=0
while (( i < ${#opts[*]} )); do
	[[ -e $propfile ]] && $RM -f $propfile
	log_must eval "$ZFS get ${opts[i]} all >$propfile"

	for ds in $allds; do
		$GREP $ds $propfile >/dev/null 2>&1
		(( $? != 0 )) && \
			log_fail "There is no property for" \
				"dataset $ds in 'get all' output."

		propnum=`$CAT $propfile | $AWK '{print $1}' | \
			$GREP "${ds}$" | $WC -l`
		ds_type=`$ZFS get -H -o value type $ds`
		case $ds_type in 
			filesystem )
				(( propnum < fspropnum )) && \
				(( failflag += 1 ))
				;;
			snapshot )
				(( propnum < snappropnum )) && \
				(( failflag += 1 ))
				;;
			volume )
				(( propnum < volpropnum )) && \
				(( failflag += 1 ))
				;;
		esac

		(( failflag != 0 )) && \
			log_fail " 'zfs get all' fails to get out " \
				"all properties for dataset $ds."
	
		(( propnum = 0 ))
		(( failflag = 0 ))
	done
	
	(( i += 1 ))
done

log_note "'zfs get' can get particular property for all datasets with that property."

function do_particular_prop_test #<property> <suitable datasets>
{
	typeset	props="$1"
	typeset ds="$2"

	for prop in ${commprops[*]}; do
		ds=`$ZFS get -H -o name $prop`

		[[ "$ds" != "$allds" ]] && \
			log_fail "The result datasets are $ds, but all suitable" \
				"datasets are $allds for the property $prop"
	done
}
	
# Here, we do a testing for user defined properties and the most common properties
# for all datasets.
commprop="type creation used referenced compressratio"
usrprop="a:b d_1:1_e 123:456"

do_particular_prop_test "$commprop" "$allds"
do_particular_prop_test "$usrprop" "$usrpropds"

log_pass "'zfs get all' works as expected."
