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

# Outline:
# For each cloned interface type, do three tests
# 1) Create and destroy it
# 2) Create, up, and destroy it
# 3) Create, disable IPv6 auto address assignment, up, and destroy it

TESTLEN=10	# seconds

atf_test_case faith_stress cleanup
faith_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy a faith(4)"
	atf_set "require.user" "root"
}
faith_stress_body()
{
	do_stress "faith"
}
faith_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case faith_up_stress cleanup
faith_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a faith(4)"
	atf_set "require.user" "root"
}
faith_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "faith" "" ""
}
faith_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case faith_ipv6_up_stress cleanup
faith_ipv6_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a faith(4) with IPv6"
	atf_set "require.user" "root"
}
faith_ipv6_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "faith" "6" ""
}
faith_ipv6_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case gif_stress cleanup
gif_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy a gif(4)"
	atf_set "require.user" "root"
}
gif_stress_body()
{
	do_stress "gif"
}
gif_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case gif_up_stress cleanup
gif_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a gif(4)"
	atf_set "require.user" "root"
}
gif_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "gif" "" "p2p"
}
gif_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case gif_ipv6_up_stress cleanup
gif_ipv6_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a gif(4) with IPv6"
	atf_set "require.user" "root"
}
gif_ipv6_up_stress_body()
{
	atf_skip "Quickly panics: rt_tables_get_rnh_ptr: fam out of bounds."
	do_up_stress "gif" "6" "p2p"
}
gif_ipv6_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case lo_stress cleanup
lo_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy an lo(4)"
	atf_set "require.user" "root"
}
lo_stress_body()
{
	do_stress "lo"
}
lo_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case lo_up_stress cleanup
lo_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy an lo(4)"
	atf_set "require.user" "root"
}
lo_up_stress_body()
{
	atf_skip "Quickly panics: GPF in rtsock_routemsg"
	do_up_stress "lo" "" ""
}
lo_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case lo_ipv6_up_stress cleanup
lo_ipv6_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy an lo(4) with IPv6"
	atf_set "require.user" "root"
}
lo_ipv6_up_stress_body()
{
	atf_skip "Quickly panics: page fault in rtsock_addrmsg"
	do_up_stress "lo" "6" ""
}
lo_ipv6_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case tap_stress cleanup
tap_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy a tap(4)"
	atf_set "require.user" "root"
}
tap_stress_body()
{
	do_stress "tap"
}
tap_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case tap_up_stress cleanup
tap_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a tap(4)"
	atf_set "require.user" "root"
}
tap_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "tap" "" ""
}
tap_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case tap_ipv6_up_stress cleanup
tap_ipv6_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a tap(4) with IPv6"
	atf_set "require.user" "root"
}
tap_ipv6_up_stress_body()
{
	atf_skip "Quickly panics: if_delmulti_locked: inconsistent ifp 0xfffff80150e44000"
	do_up_stress "tap" "6" ""
}
tap_ipv6_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case tun_stress cleanup
tun_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy a tun(4)"
	atf_set "require.user" "root"
}
tun_stress_body()
{
	do_stress "tun"
}
tun_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case tun_up_stress cleanup
tun_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a tun(4)"
	atf_set "require.user" "root"
}
tun_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "tun" "" "p2p"
}
tun_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case tun_ipv6_up_stress cleanup
tun_ipv6_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a tun(4) with IPv6"
	atf_set "require.user" "root"
}
tun_ipv6_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "tun" "6" "p2p"
}
tun_ipv6_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case vlan_stress cleanup
vlan_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy a vlan(4)"
	atf_set "require.user" "root"
}
vlan_stress_body()
{
	do_stress "vlan"
}
vlan_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case vlan_up_stress cleanup
vlan_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a vlan(4)"
	atf_set "require.user" "root"
}
vlan_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "vlan" "" ""
}
vlan_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case vlan_ipv6_up_stress cleanup
vlan_ipv6_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a vlan(4) with IPv6"
	atf_set "require.user" "root"
}
vlan_ipv6_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "vlan" "6" ""
}
vlan_ipv6_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case vmnet_stress cleanup
vmnet_stress_head()
{
	atf_set "descr" "Simultaneously create and destroy a vmnet(4)"
	atf_set "require.user" "root"
}
vmnet_stress_body()
{
	do_stress "vmnet"
}
vmnet_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case vmnet_up_stress cleanup
vmnet_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a vmnet(4)"
	atf_set "require.user" "root"
}
vmnet_up_stress_body()
{
	do_up_stress "vmnet" "" ""
}
vmnet_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_test_case vmnet_ipv6_up_stress cleanup
vmnet_ipv6_up_stress_head()
{
	atf_set "descr" "Simultaneously up and destroy a vmnet(4) with IPv6"
	atf_set "require.user" "root"
}
vmnet_ipv6_up_stress_body()
{
	atf_skip "Quickly panics: if_freemulti: protospec not NULL"
	do_up_stress "vmnet" "6" ""
}
vmnet_ipv6_up_stress_cleanup()
{
	cleanup_ifaces
}

atf_init_test_cases()
{
	# TODO: add epair(4) tests, which need a different syntax
	atf_add_test_case faith_ipv6_up_stress
	atf_add_test_case faith_stress
	atf_add_test_case faith_up_stress
	atf_add_test_case gif_ipv6_up_stress
	atf_add_test_case gif_stress
	atf_add_test_case gif_up_stress
	# Don't test lagg; it has its own test program
	atf_add_test_case lo_ipv6_up_stress
	atf_add_test_case lo_stress
	atf_add_test_case lo_up_stress
	atf_add_test_case tap_ipv6_up_stress
	atf_add_test_case tap_stress
	atf_add_test_case tap_up_stress
	atf_add_test_case tun_ipv6_up_stress
	atf_add_test_case tun_stress
	atf_add_test_case tun_up_stress
	atf_add_test_case vlan_ipv6_up_stress
	atf_add_test_case vlan_stress
	atf_add_test_case vlan_up_stress
	atf_add_test_case vmnet_ipv6_up_stress
	atf_add_test_case vmnet_stress
	atf_add_test_case vmnet_up_stress
}

do_stress()
{
	local IFACE

	IFACE=`get_iface $1`

	# First thread: create the interface
	while true; do
		ifconfig $IFACE create 2>/dev/null && \
			echo -n . >> creator_count.txt
	done &
	CREATOR_PID=$!

	# Second thread: destroy the lagg
	while true; do 
		ifconfig $IFACE destroy 2>/dev/null && \
			echo -n . >> destroyer_count.txt
	done &
	DESTROYER_PID=$!

	sleep ${TESTLEN}
	kill $CREATOR_PID
	kill $DESTROYER_PID
	echo "Created $IFACE `stat -f %z creator_count.txt` times."
	echo "Destroyed it `stat -f %z destroyer_count.txt` times."
}

# Implement the up stress tests
# Parameters
# $1	Interface class, etc "lo" or "tap"
# $2	"6" to enable IPv6 auto address assignment, anything else otherwise
# $3	p2p for point to point interfaces, anything else for normal interfaces
do_up_stress()
{
	local IFACE IPv6 MAC P2P SRCDIR

	# Configure the interface to use an RFC5737 nonrouteable addresses
	ADDR="192.0.2.2"
	DSTADDR="192.0.2.3"
	MASK="24"
	# ifconfig takes about 10ms to run.  To increase race coverage,
	# randomly delay the two commands relative to each other by 5ms either
	# way.
	MEAN_SLEEP_SECONDS=.005
	MAX_SLEEP_USECS=10000

	IFACE=`get_iface $1`
	IPV6=$2
	P2P=$3

	SRCDIR=$( atf_get_srcdir )
	if [ "$IPV6" = 6 ]; then
		ipv6_cmd="true"
	else
		ipv6_cmd="ifconfig $IFACE inet6 ifdisabled"
	fi
	if [ "$P2P" = "p2p" ]; then
		up_cmd="ifconfig $IFACE up ${ADDR}/${MASK} ${DSTADDR}"
	else
		up_cmd="ifconfig $IFACE up ${ADDR}/${MASK}"
	fi
	while true; do
		eval "$ipv6_cmd"
		{ sleep ${MEAN_SLEEP_SECONDS} && \
			eval "$up_cmd" 2> /dev/null &&
			echo -n . >> up_count.txt ; } &
		{ ${SRCDIR}/randsleep ${MAX_SLEEP_USECS} && \
			ifconfig $IFACE destroy &&
			echo -n . >> destroy_count.txt ; } &
		wait
		ifconfig $IFACE create
	done &
	LOOP_PID=$!

	sleep ${TESTLEN}
	kill $LOOP_PID
	echo "Upped ${IFACE} `stat -f %z up_count.txt` times."
	echo "Destroyed it `stat -f %z destroy_count.txt` times."
}

# Creates a new cloned interface, registers it for cleanup, and echoes it
# params: $1	Interface class name (tap, gif, etc)
get_iface()
{
	local CLASS DEV N

	CLASS=$1
	N=0
	while ! ifconfig ${CLASS}${N} create > /dev/null 2>&1; do
		if [ "$N" -ge 8 ]; then
			atf_skip "Could not create a ${CLASS} interface"
		else
			N=$(($N + 1))
		fi
	done
	local DEV=${CLASS}${N}
	# Record the device so we can clean it up later
	echo ${DEV} >> "devices_to_cleanup"
	echo ${DEV}
}


cleanup_ifaces()
{
	local DEV

	for DEV in `cat "devices_to_cleanup"`; do
		if [ ${DEV%%[0-9]*a} = "epair" ]; then
			ifconfig ${DEV}a destroy
		else
			ifconfig ${DEV} destroy
		fi
	done
	true
}
