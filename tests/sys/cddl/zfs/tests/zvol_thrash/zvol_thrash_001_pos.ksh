#!/usr/local/bin/ksh93
#
# Copyright (c) 2010 Spectra Logic Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions, and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce at minimum a disclaimer
#    substantially similar to the "NO WARRANTY" disclaimer below
#    ("Disclaimer") and any redistribution must be conditioned upon
#    including a substantially similar Disclaimer requirement for further
#    binary redistribution.
#
# NO WARRANTY
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGES.
#
# $FreeBSD$
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/include/libgnop.kshlib

# Cleanup function.  Kill each of the children.
function docleanup
{
	for CPID in $CHILDREN
	do
		echo "Killing $CPID"
		kill $CPID
	done
	for CPID in $CHILDREN
	do
		wait $CPID
	done
}

function childcleanup
{
	exit 0
}

# Wait for the timeout, and then kill the child processes.
function childrentimeout
{
	log_note "childrentimeout process waiting $1 seconds"
	sleep $1
	docleanup
}

function mk_vols
{
	ADISKS=($DISKS)				#Create an array for convenience
	N_DISKS=${#ADISKS[@]}
	N_MIRRORS=$(($N_DISKS / 2 ))
	# Use a special ksh93 expansion to generate the list of gnop devices
	GNOPS=${DISKS//~(E)([[:space:]]+|$)/.nop\1}
	setup_mirrors $N_MIRRORS $GNOPS
	for pool in `all_pools`; do
		# Create 4 ZVols per pool.  Write a geom label to each, just so
		# that we have another geom class between zvol and the vdev
		# taster.  That thwarts detection of zvols based on a geom
		# producer's class name, as was attempted by Perforce change
		# 538882
		for ((j=0; $j<4; j=$j+1)); do
			$ZFS create -V 10G $pool/testvol.$j
			glabel label testlabel$j /dev/zvol/$pool/testvol.$j
		done
	done
}

export CHILDREN=""

log_onexit docleanup

log_assert "Cause frequent device removal and arrival in the prescence of zvols.  ZFS should not crash or hang while tasting them for VDev GUIDs."
mk_vols
for p in `all_pools`
do
	#Take the first gnop in the pool
	typeset gnop
	typeset disk
	gnop=`get_disklist $p | cut -d " " -f 1`
	disk=${gnop%.nop}

	log_note "thrashing $gnop"
	trap childcleanup INT TERM && while `true`; do
	log_must destroy_gnop $disk
	$SLEEP 5
	log_must create_gnop $disk
	$SLEEP 5
	done &
	CHILDREN="$CHILDREN $!"
done

log_note "Waiting $RUNTIME seconds for potential ZFS failure"
childrentimeout $RUNTIME

log_pass
