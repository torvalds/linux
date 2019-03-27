#
#  Copyright (c) 2014 Spectra Logic Corporation
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions, and the following disclaimer,
#     without modification.
#  2. Redistributions in binary form must reproduce at minimum a disclaimer
#     substantially similar to the "NO WARRANTY" disclaimer below
#     ("Disclaimer") and any redistribution must be conditioned upon
#     including a substantially similar Disclaimer requirement for further
#     binary redistribution.
#
#  NO WARRANTY
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
#  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
#  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGES.
#
#  Authors: Alan Somers         (Spectra Logic Corporation)
#
# $FreeBSD$

atf_test_case create cleanup
create_head()
{
	atf_set "descr" "Create a lagg and assign an address"
	atf_set "require.user" "root"
}
create_body()
{
	local TAP0 TAP1 LAGG MAC

	# Configure the lagg interface to use an RFC5737 nonrouteable addresses
	ADDR="192.0.2.2"
	MASK="24"

	TAP0=`get_tap`
	TAP1=`get_tap`
	LAGG=`get_lagg`

	# Create the lagg
	ifconfig $TAP0 up
	ifconfig $TAP1 up
	atf_check ifconfig $LAGG up laggport $TAP0 laggport $TAP1 \
		${ADDR}/${MASK}
	atf_check -o match:"inet ${ADDR}" ifconfig $LAGG
	atf_check -o match:"laggport: ${TAP0}" ifconfig $LAGG
	atf_check -o match:"laggport: ${TAP1}" ifconfig $LAGG

	# Check that all members have the same MAC
	MAC=`ifconfig $LAGG | awk '/ether/ {print $2}'`
	atf_check -o match:"ether ${MAC}" ifconfig $TAP0
	atf_check -o match:"ether ${MAC}" ifconfig $TAP1

	# Check that no members have an IPv6 link-local address. IPv6
	# link-local addresses should never be merged in any way to prevent
	# scope violation.
	atf_check -o not-match:"inet6 fe80:" ifconfig $TAP0
	atf_check -o not-match:"inet6 fe80:" ifconfig $TAP1
}
create_cleanup()
{
	cleanup_tap_and_lagg
}

atf_test_case status_stress cleanup
status_stress_head()
{
	atf_set "descr" "Simultaneously query a lagg while also creating or destroying it."
	atf_set "require.user" "root"
}
status_stress_body()
{
	local TAP0 TAP1 LAGG MAC

	# Configure the lagg interface to use an RFC5737 nonrouteable addresses
	ADDR="192.0.2.2"
	MASK="24"

	TAP0=`get_tap`
	TAP1=`get_tap`
	TAP2=`get_tap`
	TAP3=`get_tap`
	LAGG=`get_lagg`

	# Up the lagg's children
	ifconfig $TAP0 inet6 ifdisabled up
	ifconfig $TAP1 inet6 ifdisabled up
	ifconfig $TAP2 inet6 ifdisabled up
	ifconfig $TAP3 inet6 ifdisabled up

	# First thread: create and destroy the lagg
	while true; do
		ifconfig $LAGG destroy 2>&1
		ifconfig $LAGG create 2>/dev/null
		ifconfig $LAGG inet6 ifdisabled
		ifconfig $LAGG up laggport $TAP0 laggport $TAP1 laggport $TAP2\
			laggport $TAP3 ${ADDR}/${MASK} 2>/dev/null
		echo -n . >> creator_count.txt
	done &
	CREATOR_PID=$!

	# Second thread: Query the lagg's status
	while true; do
		ifconfig -am 2> /dev/null > /dev/null
		echo -n . >> querier_count.txt
	done &
	QUERIER_PID=$!

	sleep 60
	kill $CREATOR_PID
	kill $QUERIER_PID
	echo "Created the lagg `stat -f %z creator_count.txt` times."
	echo "Queried its status `stat -f %z querier_count.txt` times"
}
status_stress_cleanup()
{
	cleanup_tap_and_lagg
}

atf_test_case create_destroy_stress cleanup
create_destroy_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy a lagg"
	atf_set "require.user" "root"
}
create_destroy_stress_body()
{
	local TAP0 TAP1 LAGG MAC
	
	atf_skip "Skipping this test because it easily panics the machine"

	TAP0=`get_tap`
	TAP1=`get_tap`
	TAP2=`get_tap`
	TAP3=`get_tap`
	LAGG=`get_lagg`

	# Up the lagg's children
	ifconfig $TAP0 inet6 ifdisabled up
	ifconfig $TAP1 inet6 ifdisabled up
	ifconfig $TAP2 inet6 ifdisabled up
	ifconfig $TAP3 inet6 ifdisabled up

	# First thread: create the lagg
	while true; do
		ifconfig $LAGG create 2>/dev/null && \
			echo -n . >> creator_count.txt
	done &
	CREATOR_PID=$!

	# Second thread: destroy the lagg
	while true; do 
		ifconfig $LAGG destroy 2>/dev/null && \
			echo -n . >> destroyer_count.txt
	done &
	DESTROYER_PID=$!

	sleep 60
	kill $CREATOR_PID
	kill $DESTROYER_PID
	echo "Created the lagg `stat -f %z creator_count.txt` times."
	echo "Destroyed it `stat -f %z destroyer_count.txt` times."
}
create_destroy_stress_cleanup()
{
	cleanup_tap_and_lagg
}

# This test regresses a panic that is particular to LACP.  If the child's link
# state changes while the lagg is being destroyed, lacp_linkstate can
# use-after-free.  The problem is compounded by two factors:
# 1) In SpectraBSD, downing the parent will also down the child
# 2) The cxgbe driver will show the link state as "no carrier" as soon as you
#    down the interface.
# TeamTrack: P2_30328
atf_test_case lacp_linkstate_destroy_stress cleanup
lacp_linkstate_destroy_stress_head()
{
	atf_set "descr" "Simultaneously destroy an LACP lagg and change its childrens link states"
	atf_set "require.user" "root"
}
lacp_linkstate_destroy_stress_body()
{
	local TAP0 TAP1 LAGG MAC SRCDIR
	
	# Configure the lagg interface to use an RFC5737 nonrouteable addresses
	ADDR="192.0.2.2"
	MASK="24"
	# ifconfig takes about 10ms to run.  To increase race coverage,
	# randomly delay the two commands relative to each other by 5ms either
	# way.
	MEAN_SLEEP_SECONDS=.005
	MAX_SLEEP_USECS=10000

	TAP0=`get_tap`
	TAP1=`get_tap`
	LAGG=`get_lagg`

	# Up the lagg's children
	ifconfig $TAP0 inet6 ifdisabled up
	ifconfig $TAP1 inet6 ifdisabled up

	SRCDIR=$( atf_get_srcdir )
	while true; do
		ifconfig $LAGG inet6 ifdisabled
		# We must open the tap devices to change their link states
		cat /dev/$TAP0 > /dev/null &
		CAT0_PID=$!
		cat /dev/$TAP1 > /dev/null &
		CAT1_PID=$!
		ifconfig $LAGG up laggport $TAP0 laggport $TAP1 \
			${ADDR}/${MASK} 2> /dev/null &&
		{ sleep ${MEAN_SLEEP_SECONDS} && \
			kill $CAT0_PID &&
			kill $CAT1_PID &&
			echo -n . >> linkstate_count.txt ; } &
		{ ${SRCDIR}/randsleep ${MAX_SLEEP_USECS} && \
			ifconfig $LAGG destroy &&
			echo -n . >> destroy_count.txt ; } &
		wait
		ifconfig $LAGG create
	done &
	LOOP_PID=$!

	sleep 60
	kill $LOOP_PID
	echo "Disconnected the children `stat -f %z linkstate_count.txt` times."
	echo "Destroyed the lagg `stat -f %z destroy_count.txt` times."
}
lacp_linkstate_destroy_stress_cleanup()
{
	cleanup_tap_and_lagg
}

atf_test_case up_destroy_stress cleanup
up_destroy_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a lagg"
	atf_set "require.user" "root"
}
up_destroy_stress_body()
{
	local TAP0 TAP1 LAGG MAC SRCDIR

	atf_skip "Skipping this test because it panics the machine fairly often"
	
	# Configure the lagg interface to use an RFC5737 nonrouteable addresses
	ADDR="192.0.2.2"
	MASK="24"
	# ifconfig takes about 10ms to run.  To increase race coverage,
	# randomly delay the two commands relative to each other by 5ms either
	# way.
	MEAN_SLEEP_SECONDS=.005
	MAX_SLEEP_USECS=10000

	TAP0=`get_tap`
	TAP1=`get_tap`
	TAP2=`get_tap`
	TAP3=`get_tap`
	LAGG=`get_lagg`

	# Up the lagg's children
	ifconfig $TAP0 inet6 ifdisabled up
	ifconfig $TAP1 inet6 ifdisabled up
	ifconfig $TAP2 inet6 ifdisabled up
	ifconfig $TAP3 inet6 ifdisabled up

	SRCDIR=$( atf_get_srcdir )
	while true; do
		ifconfig $LAGG inet6 ifdisabled
		{ sleep ${MEAN_SLEEP_SECONDS} && \
			ifconfig $LAGG up laggport $TAP0 laggport $TAP1 \
				laggport $TAP2 laggport $TAP3 \
				${ADDR}/${MASK} 2> /dev/null &&
			echo -n . >> up_count.txt ; } &
		{ ${SRCDIR}/randsleep ${MAX_SLEEP_USECS} && \
			ifconfig $LAGG destroy &&
			echo -n . >> destroy_count.txt ; } &
		wait
		ifconfig $LAGG create
	done &
	LOOP_PID=$!

	sleep 60
	kill $LOOP_PID
	echo "Upped the lagg `stat -f %z up_count.txt` times."
	echo "Destroyed it `stat -f %z destroy_count.txt` times."
}
up_destroy_stress_cleanup()
{
	cleanup_tap_and_lagg
}

atf_test_case set_ether cleanup
set_ether_head()
{
	atf_set "descr" "Set a lagg's ethernet address"
	atf_set "require.user" "root"
}
set_ether_body()
{
	local TAP0 TAP1 LAGG MAC

	# Configure the lagg interface to use an RFC5737 nonrouteable addresses
	ADDR="192.0.2.2"
	MASK="24"
	MAC="00:11:22:33:44:55"

	TAP0=`get_tap`
	TAP1=`get_tap`
	LAGG=`get_lagg`

	# Create the lagg
	ifconfig $TAP0 up
	ifconfig $TAP1 up
	atf_check ifconfig $LAGG up laggport $TAP0 laggport $TAP1 \
		${ADDR}/${MASK}

	# Change the lagg's ethernet address
	atf_check ifconfig $LAGG ether ${MAC}

	# Check that all members have the same MAC
	atf_check -o match:"ether ${MAC}" ifconfig $LAGG
	atf_check -o match:"ether ${MAC}" ifconfig $TAP0
	atf_check -o match:"ether ${MAC}" ifconfig $TAP1
}
set_ether_cleanup()
{
	cleanup_tap_and_lagg
}

atf_test_case updown cleanup
updown_head()
{
	atf_set "descr" "upping or downing a lagg ups or downs its children"
	atf_set "require.user" "root"
}
updown_body()
{
	local TAP0 TAP1 LAGG MAC

	atf_expect_fail "PR 226144 Upping a lagg interrface should automatically up its children"
	# Configure the lagg interface to use an RFC5737 nonrouteable addresses
	ADDR="192.0.2.2"
	MASK="24"
	MAC="00:11:22:33:44:55"

	TAP0=`get_tap`
	TAP1=`get_tap`
	LAGG=`get_lagg`

	# Create the lagg
	ifconfig $TAP0 up
	ifconfig $TAP1 up
	atf_check ifconfig $LAGG up laggport $TAP0 laggport $TAP1 \
		${ADDR}/${MASK}

	# Down the lagg
	ifconfig $LAGG down
	atf_check -o not-match:"flags=.*\<UP\>" ifconfig $LAGG
	atf_check -o not-match:"flags=.*\<UP\>" ifconfig $TAP0
	atf_check -o not-match:"flags=.*\<UP\>" ifconfig $TAP1
	# Up the lagg again
	ifconfig $LAGG up
	atf_check -o match:"flags=.*\<UP\>" ifconfig $LAGG
	atf_check -o match:"flags=.*\<UP\>" ifconfig $TAP0
	atf_check -o match:"flags=.*\<UP\>" ifconfig $TAP1

	# Check that no members have acquired an IPv6 link-local address by
	# virtue of being upped. IPv6 link-local addresses should never be
	# merged in any way to prevent scope violation.
	atf_check -o not-match:"inet6 fe80:" ifconfig $TAP0
	atf_check -o not-match:"inet6 fe80:" ifconfig $TAP1
}
updown_cleanup()
{
	cleanup_tap_and_lagg
}

# Check for lock-order reversals.  For best results, this test should be run
# last.
atf_test_case witness
witness_head()
{
	atf_set "descr" "Check witness(4) for lock-order reversals in if_lagg"
}
witness_body()
{
	if [ `sysctl -n debug.witness.watch` -ne 1 ]; then
		atf_skip "witness(4) is not enabled"
	fi
	if `sysctl -n debug.witness.badstacks | grep -q 'at lagg_'`; then
		sysctl debug.witness.badstacks
		atf_fail "Lock-order reversals involving if_lagg.c detected"
	fi
}

atf_init_test_cases()
{
	atf_add_test_case create
	atf_add_test_case create_destroy_stress
	atf_add_test_case lacp_linkstate_destroy_stress
	atf_add_test_case set_ether
	atf_add_test_case status_stress
	atf_add_test_case up_destroy_stress
	atf_add_test_case updown
	# For best results, keep the witness test last
	atf_add_test_case witness
}


# Creates a new tap(4) interface, registers it for cleanup, and echoes it
get_tap()
{
	local TAPN=0
	while ! ifconfig tap${TAPN} create > /dev/null 2>&1; do
		if [ "$TAPN" -ge 8 ]; then
			atf_skip "Could not create a tap(4) interface"
		else
			TAPN=$(($TAPN + 1))
		fi
	done
	local TAPD=tap${TAPN}
	# Record the TAP device so we can clean it up later
	echo ${TAPD} >> "devices_to_cleanup"
	echo ${TAPD}
}

# Creates a new lagg(4) interface, registers it for cleanup, and echoes it
get_lagg()
{
	local LAGGN=0
	while ! ifconfig lagg${LAGGN} create > /dev/null 2>&1; do
		if [ "$LAGGN" -ge 8 ]; then
			atf_skip "Could not create a lagg(4) interface"
		else
			LAGGN=$(($LAGGN + 1))
		fi
	done
	local LAGGD=lagg${LAGGN}
	# Record the lagg device so we can clean it up later
	echo ${LAGGD} >> "devices_to_cleanup"
	echo ${LAGGD}
}

cleanup_tap_and_lagg()
{
	local DEV

	for DEV in `cat "devices_to_cleanup"`; do
		ifconfig ${DEV} destroy
	done
	true
}
