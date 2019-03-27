#!/bin/sh
#-
# Copyright (c) 2012 Cisco Systems, Inc.
# All rights reserved.
#
# This software was developed by Bjoern Zeeb under contract to
# Cisco Systems, Inc..
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

# We will use the RFC5180 (and Errata) benchmarking working group prefix
# 2001:0002::/48 for testing.
PREFIX="2001:2:"

# Set IFACE to the real interface you want to run the test on.
: ${IFACE:=lo0}

# Number of seconds to wait for peer node to synchronize for test.
: ${WAITS:=120}

# Control port we use to exchange messages between nodes to sync. tests, etc.
: ${CTRLPORT:=6666}

# Get the number of FIBs from the kernel.
RT_NUMFIBS=`sysctl -n net.fibs`

OURADDR="2001:2:ff00::1"
PEERADDR="2001:2:ff00::2"
PEERLINKLOCAL=""

# By default all commands must succeed.  Individual tests may disable this
# temporary.
set -e

# Debug magic.
case "${DEBUG}" in
42)	set -x ;;
esac



#
# Helper functions.
#
check_rc()
{
	local _rc _exp _testno _testname _msg _r
	_rc=$1
	_exp=$2
	_testno=$3
	_testname="$4"
	_msg="$5"

	_r="not ok"
	if test ${_rc} -eq ${_exp}; then
		_r="ok"
	fi
	echo "${_r} ${_testno} ${_testname} # ${_msg} ${_rc} ${_exp}"
}

print_debug()
{
	local _msg
	_msg="$*"

	case ${DEBUG} in
	''|0)	;;
	*)	echo "DEBUG: ${_msg}" >&2 ;;
	esac
}

die()
{
	local _msg
	_msg="$*"

	echo "ERROR: ${_msg}" >&2
	exit 1
}

#
# Test functions.
#

# Make sure the local link-local and global addresses are reachable
# from all FIBs.
check_local_addr()
{
	local _l i testno

	print_debug "Setting up interface ${IFACE}"
	ifconfig ${IFACE} inet6 ${OURADDR}/64 alias up
	_l=`ifconfig ${IFACE} | awk '/inet6 fe80:/ { print $2 }'`

	# Let things settle.
	print_debug "Waiting 4 seconds for things to settle"
	sleep 4

	printf "1..%d\n" `expr 2 \* ${RT_NUMFIBS}`
	testno=1
	i=0
	set +e
	while test ${i} -lt ${RT_NUMFIBS}; do
		print_debug "Testing FIB ${i}"

		setfib -F${i} ping6 -n -c1 ${_l} > /dev/null 2>&1
		check_rc $? 0 ${testno} "check_local_addr_${i}_l" \
		    "FIB ${i} ${_l}"
		testno=$((testno + 1))

		setfib -F${i} ping6 -n -c1 ${OURADDR} > /dev/null 2>&1
		check_rc $? 0 ${testno} "check_local_addr_${i}_a" \
		    "FIB ${i} ${OURADDR}"
		testno=$((testno + 1))

		i=$((i + 1))
	done
	set -e
	ifconfig ${IFACE} inet6 ${OURADDR}/64 -alias
}


# Cloned tun(4) devices behave differently on FIB 0 vs. FIB 1..n after creation
# (they also do in IPv4).
check_local_tun()
{
	local _l i testno IFACE _msg

	IFACE=tun42
	print_debug "Setting up interface ${IFACE}"
	ifconfig ${IFACE} create
	ifconfig ${IFACE} inet6 ${OURADDR}/64 alias up
	_l=`ifconfig ${IFACE} | awk '/inet6 fe80:/ { print $2 }'`

	# Let things settle.
	print_debug "Waiting 4 seconds for things to settle"
	sleep 4

	printf "1..%d\n" `expr 2 \* ${RT_NUMFIBS}`
	testno=1
	_msg=""
	i=0
	set +e
	while test ${i} -lt ${RT_NUMFIBS}; do
		print_debug "Testing FIB ${i}"
		if test ${i} -gt 0; then
			# Flag the well known behaviour as such.
			_msg="TODO "
		fi

		setfib -F${i} ping6 -n -c1 ${_l} > /dev/null 2>&1
		check_rc $? 0 ${testno} "check_local_addr_${i}_l" \
		    "${_msg}FIB ${i} ${_l}"
		testno=$((testno + 1))

		setfib -F${i} ping6 -n -c1 ${OURADDR} > /dev/null 2>&1
		check_rc $? 0 ${testno} "check_local_addr_${i}_a" \
		    "${_msg}FIB ${i} ${OURADDR}"
		testno=$((testno + 1))

		i=$((i + 1))
	done
	set -e
	ifconfig ${IFACE} inet6 ${OURADDR}/64 -alias
	ifconfig ${IFACE} destroy
}

check_remote_up()
{
	local _l i testno

	print_debug "Setting up interface ${IFACE}"
	ifconfig ${IFACE} inet6 ${OURADDR}/64 alias up
	_l=`ifconfig ${IFACE} | awk '/inet6 fe80:/ { print $2 }'`

	# Let things settle.
	print_debug "Waiting 4 seconds for things to settle"
	sleep 4



}

send_greeting()
{
        local _l _greeting _keyword _fib _fibs _linklocal

	print_debug "Setting up interface ${IFACE}"
	ifconfig ${IFACE} inet6 ${OURADDR}/64 alias up
	_l=`ifconfig ${IFACE} | awk '/inet6 fe80:/ { print $2 }'`

	# Let things settle.
	print_debug "Waiting 4 seconds for things to settle"
	sleep 4

	# Cleanup firewall and install rules to always allow NS/NA to succeed.
	# The latter is needed to allow indvidiual less specific later rules
	# from test cases to just disallow any IPv6 traffic on a matching FIB.
	ipfw -f flush > /dev/null 2>&1
	ipfw add 65000 permit ip from any to any > /dev/null 2>&1
	ipfw add 5 permit ipv6-icmp from any to any icmp6types 135,136 fib 0 \
	    via ${IFACE} out > /dev/null 2>&1

	set +e
	i=0
	rc=-1
	while test ${i} -lt ${WAITS} -a ${rc} -ne 0; do
		print_debug "Sending greeting #${i} to peer"
		_greeting=`echo "SETUP ${RT_NUMFIBS} ${_l}" | \
		    nc -6 -w 1 ${PEERADDR} ${CTRLPORT}`
		rc=$?
		i=$((i + 1))
		# Might sleep longer in total but better than to DoS
		# and not get anywhere.
		sleep 1
	done
	set -e

	read _keyword _fibs _linklocal <<EOI
${_greeting}
EOI
	print_debug "_keyword=${_keyword}"
	print_debug "_fibs=${_fibs}"
	print_debug "_linklocal=${_linklocal}"
	case ${_keyword} in
	SETUP)	;;
	*)	die "Got invalid keyword in greeting: ${_greeting}"
	;;
	esac
	if test ${_fibs} -ne ${RT_NUMFIBS}; then
		die "Number of FIBs not matching ours (${RT_NUMFIBS}) in" \
		    "greeting: ${_greeting}"
	fi
	PEERLINKLOCAL=${_linklocal}

	# Swap the zoneid to the local interface scope.
	PEERLINKLOCAL=${PEERLINKLOCAL%%\%*}"%${IFACE}"

	print_debug "Successfully exchanged greeting. Peer at ${PEERLINKLOCAL}"
}

cleanup()
{

	# Cleanup ipfw.
	ipfw delete 5 > /dev/null 2>&1

	print_debug "Removing address from interface ${IFACE}"
	ifconfig ${IFACE} inet6 ${OURADDR}/64 -alias
}

testtx_icmp6()
{
	local _n _transfer i testno _txt _fibtxt _rc _ec _p
	_n="$1"
	_transfer=$2

	printf "1..%d\n" `expr 2 \* ${RT_NUMFIBS}`
	testno=1
	set +e
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		_txt="${_n}${i}"
		print_debug "Testing ${_txt}"
		_fibtxt=`echo "${_txt}" | hd -v | cut -b11-60 | tr -d ' \r\n'`

		eval _rc="\${rc_${i}_l}"
		setfib -F${i} ping6 -n -c1 -p ${_fibtxt} \
		    ${PEERLINKLOCAL} > /dev/null 2>&1
		_ec=$?
		# We need to normalize the exit code of ping6.
		case ${_ec} in
		0)	;;
		*)	_ec=1 ;;
		esac
		check_rc ${_ec} ${_rc} ${testno} "${_txt}_l" \
		    "FIB ${i} ${PEERLINKLOCAL}"
		testno=$((testno + 1))

		# If doing multiple transfer networks, replace PEERADDR.
		_p="${PEERADDR}"
		case ${_transfer} in
		1)	PEERADDR=2001:2:${i}::2 ;;
		esac

		eval _rc="\${rc_${i}_a}"
		setfib -F${i} ping6 -n -c1 -p ${_fibtxt} ${PEERADDR} \
		    > /dev/null 2>&1
		_ec=$?
		# We need to normalize the exit code of ping6.
		case ${_ec} in
		0)	;;
		*)	_ec=1 ;;
		esac
		check_rc ${_ec} ${_rc} ${testno} "${_txt}_a" \
		    "FIB ${i} ${PEERADDR}"
		testno=$((testno + 1))

		# Restore PEERADDR.
		PEERADDR="${_p}"

		i=$((i + 1))
	done
	set -e
}

nc_send_recv()
{
	local _fib _loops _msg _expreply _addr _port _opts i
	_fib=$1
	_loops=$2
	_msg="$3"
	_expreply="$4"
	_addr=$5
	_port=$6
	_opts="$7"

	i=0
	while test ${i} -lt ${_loops}; do
		i=$((i + 1))
		case "${USE_SOSETFIB}" in
		1)
			_reply=`echo "${_msg}" | \
			    nc -V ${_fib} ${_opts} ${_addr} ${_port}`
			;;
		*)
			_reply=`echo "${_msg}" | \
			    setfib -F${_fib} nc ${_opts} ${_addr} ${_port}`
			;;
		esac
		if test "${_reply}" != "${_expreply}"; then
			if test ${i} -lt ${_loops}; then
				sleep 1
			else
			# Must let caller decide how to handle the error.
			#	die "Got invalid reply from peer." \
			#	    "Expected '${_expreply}', got '${_reply}'"
				return 1
			fi
		else
			break
		fi
	done
	return 0
}

testtx_tcp_udp()
{
	local _n _o _f testno i _fibtxt
	_n="$1"
	_o="$2"
	_f="$3"

	printf "1..%d\n" `expr 2 \* ${RT_NUMFIBS}`
	testno=1
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		print_debug "Testing ${_f} ${i}"

		eval _rc="\${rc_${i}_l}"
		_fibtxt="${_n}_${i}_l ${_f} ${i} ${PEERLINKLOCAL}"
		nc_send_recv ${i} 1 "${_fibtxt}" "${_fibtxt}" ${PEERLINKLOCAL} \
		    ${CTRLPORT} "-6 ${_o} -w1"
		check_rc $? ${_rc} ${testno} "${_fibtxt}"
		testno=$((testno + 1))

		eval _rc="\${rc_${i}_a}"
		_fibtxt="${_n}_${i}_a ${_f} ${i} ${PEERADDR}"
		nc_send_recv ${i} 1 "${_fibtxt}" "${_fibtxt}" ${PEERADDR} \
		    ${CTRLPORT} "-6 ${_o} -w1"
		check_rc $? ${_rc} ${testno} "${_fibtxt}"
		testno=$((testno + 1))

		i=$((i + 1))
	done
}

# setfib TCP|UDP/IPv6 test on link-local and global address of peer from all FIBs.
testtx_ulp6_connected()
{
	local _fibtxt _reply _n _o _rc _fib _f _opts
	_n=$1
	_o="$2"
	_fib=$3

	case "${USE_SOSETFIB}" in
	1) _f="SO_SETFIB" ;;
	*) _f="SETFIB" ;;
	esac

	if test "${_o}" = "-i" -a "${_f}" = "SO_SETFIB"; then
		print_debug "Skipping icmp6 tests for SO_SETFIB."
		return 0
	fi

	# Clear the neighbor table to get deterministic runs.
	ndp -cn > /dev/null 2>&1

	case "${_o}" in
	-i)	_opts="" ;;		# Use TCP for START/DONE.
	*)	_opts="${_o}" ;;
	esac

	set +e
	# Let peer know that we are about to start.
	_msg="START ${_n}"
	nc_send_recv ${_fib} ${WAITS} "${_msg}" "${_msg}" ${PEERADDR} \
	    ${CTRLPORT} "-6 ${_opts} -w1"
	case $? in
	0)	;;
	*)	die "Got invalid reply from peer." \
		    "Expected '${_msg}', got '${_reply}'" ;;
	esac

	case "${_o}" in
	-i)	testtx_icmp6 "${_n}" ;;
	*)	testtx_tcp_udp "${_n}" "${_o}" "${_f}" ;;
	esac

	# Let peer know that we are done with this test to move to next.
	# This must immediately succeed.
	_msg="DONE ${_n}"
	nc_send_recv ${_fib} ${WAITS} "${_msg}" "${_msg}" ${PEERADDR} \
	    ${CTRLPORT} "-6 ${_opts} -w1"
	case $? in
	0)	;;
	*)	die "Got invalid reply from peer." \
		    "Expected '${_msg}', got '${_reply}'" ;;
	esac
	set -e

	print_debug "Successfully received status update '${_reply}'."
}

################################################################################
#
# ping6|TCP/UDP connect link-local and global address of peer from all FIBs.
# Default reachability test.
#
testtx_icmp6_connected()
{
	local i

	# Setup expected return values.
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		eval rc_${i}_l=0
		eval rc_${i}_a=0
		i=$((i + 1))
	done

	testtx_ulp6_connected "testtx_icmp6_connected" "-i" 0
}

testtx_tcp6_connected()
{
	local i

	# Setup expected return values.
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		eval rc_${i}_l=0
		eval rc_${i}_a=0
		i=$((i + 1))
	done

	testtx_ulp6_connected testtx_tcp6_connected "" 0
}

testtx_udp6_connected()
{
	local i

	# Setup expected return values.
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		eval rc_${i}_l=0
		eval rc_${i}_a=0
		i=$((i + 1))
	done

	testtx_ulp6_connected testtx_udp6_connected "-u" 0
}

################################################################################
#
# Use ipfw to return unreach messages for all but one FIB.  Rotate over all.
# Making sure error messages are properly returned.
#
testtx_ulp6_connected_blackhole()
{
	local fib i _n _o
	_n="$1"
	_o="$2"

	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do

		print_debug "${_n} ${fib}"

		# Setup expected return values.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			ipfw delete $((100 + i)) > /dev/null 2>&1 || true
			case ${i} in
			${fib})
				eval rc_${i}_l=0
				eval rc_${i}_a=0
				;;
			*)	eval rc_${i}_l=1
				eval rc_${i}_a=1
				ipfw add $((100 + i)) unreach6 admin-prohib \
				    ip6 from any to any fib ${i} via ${IFACE} \
				    out > /dev/null 2>&1
				;;
			esac
			i=$((i + 1))
		done

		testtx_ulp6_connected "${_n}${fib}" "${_o}" ${fib}
		case ${DEBUG} in
		''|0)	;;
		*)	ipfw show ;;
		esac
		fib=$((fib + 1))
	done
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		ipfw delete $((100 + fib)) > /dev/null 2>&1 || true
		fib=$((fib + 1))
	done
}

testtx_icmp6_connected_blackhole()
{

	testtx_ulp6_connected_blackhole \
	    "testtx_icmp6_connected_blackhole" "-i"
}

testtx_tcp6_connected_blackhole()
{

	testtx_ulp6_connected_blackhole \
	    "testtx_tcp6_connected_blackhole" ""
}

testtx_udp6_connected_blackhole()
{

	testtx_ulp6_connected_blackhole \
	    "testtx_udp6_connected_blackhole" "-u"
}

################################################################################
#
# Setup a different transfer net on each FIB.  Delete all but one connected
# route in all FIBs (e.g. FIB 0 uses prefix 0, FIB 1 uses prefix 1 , ...).
#
# Need to tag NS/NA incoming to the right FIB given the default FIB does not
# know about the prefix and thus cannot do proper source address lookups for
# replying otherwise.   Use ipfw.
#
testtx_ulp6_connected_transfernets()
{
	local fib i _n _o _p
	_n="$1"
	_o="$2"

	# Setup transfer networks and firewall.
	ipfw delete 10 > /dev/null 2>&1 || true
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 alias
		ipfw add 10 setfib ${fib} ipv6-icmp from 2001:2:${fib}::/64 \
		    to any ip6 icmp6types 135,136 via ${IFACE} in \
		    > /dev/null 2>&1
		# Remove connected routes from all but matching FIB.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			case ${i} in
			${fib});;
			*)	setfib -F${i} route delete -inet6 \
				    -net 2001:2:${fib}:: > /dev/null 2>&1
				;;
			esac
			i=$((i + 1))
		done
		fib=$((fib + 1))
	done

	# Save PEERADDR
	_p=${PEERADDR}

	# Run tests.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		PEERADDR=2001:2:${fib}::2

		print_debug "${_n} ${fib}"

		# Setup expected return values.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			eval rc_${i}_l=0
			case ${i} in
			${fib})
				eval rc_${i}_a=0
				;;
			*)	eval rc_${i}_a=1
				;;
			esac
			i=$((i + 1))
		done

		testtx_ulp6_connected "${_n}${fib}" "${_o}" ${fib}
		fib=$((fib + 1))
	done

	# Restore PEERADDR
	PEERADDR=${_p}

	# Cleanup transfer networks and firewall.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias
		fib=$((fib + 1))
	done
	ipfw delete 10 > /dev/null 2>&1
}

testtx_icmp6_connected_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_icmp6_connected_transfernets" "-i"
}

testtx_tcp6_connected_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_tcp6_connected_transfernets" ""
}

testtx_udp6_connected_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_udp6_connected_transfernets" "-u"
}

################################################################################
#
# Setup a different transfernet on each FIB.  Delete all but one connected
# route in all FIBs (e.g. FIB 0 uses prefix 0, FIB 1 uses prefix 1 , ...).
#
# Need to tag NS/NA incoming to the right FIB given the default FIB does not
# know about the prefix and thus cannot do proper source address lookups for
# replying otherwise.   Use ifconfig IFACE fib.
#
testtx_ulp6_connected_ifconfig_transfernets()
{
	local fib i _n _o _p
	_n="$1"
	_o="$2"

	# Setup transfer networks.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 alias
		# Remove connected routes from all but matching FIB.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			case ${i} in
			${fib});;
			*)	setfib -F${i} route delete -inet6 \
				    -net 2001:2:${fib}:: > /dev/null 2>&1
				;;
			esac
			i=$((i + 1))
		done
		fib=$((fib + 1))
	done

	# Save PEERADDR
	_p=${PEERADDR}

	# Run tests.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		PEERADDR=2001:2:${fib}::2

		print_debug "${_n} ${fib}"

		# Setup expected return values.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			eval rc_${i}_l=0
			case ${i} in
			${fib})
				eval rc_${i}_a=0
				;;
			*)	eval rc_${i}_a=1
				;;
			esac
			i=$((i + 1))
		done

		ifconfig ${IFACE} fib ${fib}

		testtx_ulp6_connected "${_n}${fib}" "${_o}" ${fib}
		fib=$((fib + 1))
	done

	# Restore PEERADDR
	PEERADDR=${_p}

	# Cleanup transfer networks.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias
		fib=$((fib + 1))
	done
	ifconfig ${IFACE} fib 0
}

testtx_icmp6_connected_ifconfig_transfernets()
{

	testtx_ulp6_connected_ifconfig_transfernets \
	    "testtx_icmp6_connected_ifconfig_transfernets" "-i"
}


testtx_tcp6_connected_ifconfig_transfernets()
{

	testtx_ulp6_connected_ifconfig_transfernets \
	    "testtx_tcp6_connected_ifconfig_transfernets" ""
}

testtx_udp6_connected_ifconfig_transfernets()
{

	testtx_ulp6_connected_ifconfig_transfernets \
	    "testtx_udp6_connected_ifconfig_transfernets" "-u"
}

################################################################################
#
# Make destination reachable through the same default route in each FIB only.
# Run standard reachability test.
#
testtx_ulp6_gateway()
{
	local fib i _n _o _p
	_n="$1"
	_o="$2"

	# Setup default gateway and expected error codes.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} route delete -inet6 -net default \
		    > /dev/null 2>&1 || true
		setfib -F${fib} route add -inet6 -net default ${PEERADDR} \
		    > /dev/null 2>&1
		case "${_o}" in
		-i) eval rc_${fib}_l=0 ;;	# ICMPv6 will succeed
		*)  eval rc_${fib}_l=1 ;;
		esac
		eval rc_${fib}_a=0
		fib=$((fib + 1))
	done

	# Save PEERADDR
	_p=${PEERADDR}
	PEERADDR="2001:2:ff01::2"

	# Run tests.
	print_debug "${_n}"
	testtx_ulp6_connected "${_n}" "${_o}" 0

	# Restore PEERADDR
	PEERADDR=${_p}

	# Cleanup transfer networks.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} route delete -inet6 -net default \
		    > /dev/null 2>&1
		fib=$((fib + 1))
	done
}

testtx_icmp6_gateway()
{

	testtx_ulp6_gateway "testtx_icmp6_gateway" "-i"
}

testtx_tcp6_gateway()
{

	testtx_ulp6_gateway "testtx_tcp6_gateway" ""
}

testtx_udp6_gateway()
{

	testtx_ulp6_gateway "testtx_udp6_gateway" "-u"
}

################################################################################
#
# Make destination reachable through a different default route in each FIB.
# Generate a dedicated transfer network for that in each FIB.  Delete all but
# one connected route in all FIBs (e.g. FIB 0 uses prefix 0, ...).
#
# Have a default route present in each FIB all time.
#
# Need to tag NS/NA incoming to the right FIB given the default FIB does not
# know about the prefix and thus cannot do proper source address lookups for
# replying otherwise.   Use ipfw.
#
#
testtx_ulp6_transfernets_gateways()
{
	local fib i _n _o _p
	_n="$1"
	_o="$2"

	# Setup transfer networks, default routes, and firewall.
	fib=0
	ipfw delete 10 > /dev/null 2>&1 || true
	while test ${fib} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 alias \
		    > /dev/null 2>&1
		ipfw add 10 setfib ${fib} ipv6-icmp \
		    from 2001:2:${fib}::/64 to any ip6 icmp6types 135,136 \
		    via ${IFACE} in > /dev/null 2>&1
		# Remove connected routes from all but matching FIB.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			case ${i} in
			${fib});;
			*)	setfib -F${i} route delete -inet6 \
				    -net 2001:2:${fib}:: > /dev/null 2>&1
				;;
			esac
			i=$((i + 1))
		done
		# Add default route.
		setfib -F${fib} route delete -inet6 -net default \
		    > /dev/null 2>&1 || true
		setfib -F${fib} route add -inet6 -net default \
		    2001:2:${fib}::2 > /dev/null 2>&1
		fib=$((fib + 1))
	done

	# Save PEERADDR
	_p=${PEERADDR}
	PEERADDR="2001:2:ff01::2"

	# Setup expected return values.
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		case "${_o}" in
		-i) eval rc_${i}_l=0 ;;	# ICMPv6 will succeed
		*)  eval rc_${i}_l=1 ;;
		esac
		eval rc_${i}_a=0
		i=$((i + 1))
	done

	# Run tests.
	print_debug "${_n}"
	testtx_ulp6_connected "${_n}" "${_o}" 0

	# Restore PEERADDR
	PEERADDR=${_p}

	# Cleanup default routes, transfer networks, and firewall.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} route delete -inet6 -net default \
		    > /dev/null 2>&1
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias \
		    > /dev/null 2>&1
		fib=$((fib + 1))
	done
	ipfw delete 10 > /dev/null 2>&1
}

testtx_icmp6_transfernets_gateways()
{

	testtx_ulp6_transfernets_gateways \
	    "testtx_icmp6_transfernets_gateways" "-i"
}

testtx_tcp6_transfernets_gateways()
{

	testtx_ulp6_transfernets_gateways \
	    "testtx_tcp6_transfernets_gateways" ""
}

testtx_udp6_transfernets_gateways()
{

	testtx_ulp6_transfernets_gateways \
	    "testtx_udp6_transfernets_gateways" "-u"
}

################################################################################
#
# Make destination reachable through a different default route in each FIB.
# Generate a dedicated transfer network for that in each FIB.  Delete all but
# one connected route in all FIBs (e.g. FIB 0 uses prefix 0, ...).
#
# Only have a default route present in 1 FIB at a time.
#
# Need to tag NS/NA incoming to the right FIB given the default FIB does not
# know about the prefix and thus cannot do proper source address lookups for
# replying otherwise.   Use ipfw.
#
testtx_ulp6_transfernets_gateway()
{
	local fib i _n _o _p
	_n="$1"
	_o="$2"

	# Setup transfer networks, default routes, and firewall.
	fib=0
	ipfw delete 10 > /dev/null 2>&1 || true
	while test ${fib} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 alias \
		    > /dev/null 2>&1
		ipfw add 10 setfib ${fib} ipv6-icmp \
		    from 2001:2:${fib}::/64 to any ip6 icmp6types 135,136 \
		    via ${IFACE} in > /dev/null 2>&1
		# Remove connected routes from all but matching FIB.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			case ${i} in
			${fib});;
			*)	setfib -F${i} route delete -inet6 \
				    -net 2001:2:${fib}:: > /dev/null 2>&1
				;;
			esac
			i=$((i + 1))
		done
		fib=$((fib + 1))
	done

	# Save PEERADDR
	_p=${PEERADDR}
	PEERADDR="2001:2:ff01::2"

	# Run tests.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do

		print_debug "${_n} ${fib}"

		# Setup expected return values.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			case "${_o}" in
			-i) eval rc_${i}_l=0 ;;	# ICMPv6 will succeed
			*)  eval rc_${i}_l=1 ;;
			esac
			case ${i} in
			${fib})
				eval rc_${i}_a=0
				;;
			*)	eval rc_${i}_a=1
				;;
			esac
			i=$((i + 1))
		done

		# Add default route.
		setfib -F${fib} route delete -inet6 -net default \
		    > /dev/null 2>&1 || true
		setfib -F${fib} route add -inet6 -net default \
		    2001:2:${fib}::2 > /dev/null 2>&1

		testtx_ulp6_connected "${_n}${fib}" "${_o}" ${fib}

		# Delete default route again.
		setfib -F${fib} route delete -inet6 -net default \
		    > /dev/null 2>&1
		fib=$((fib + 1))
	done

	# Restore PEERADDR
	PEERADDR=${_p}

	# Cleanup default routes, transfer networks, and firewall.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} route delete -inet6 -net default \
		    > /dev/null 2>&1
		ifconfig ${IFACE} inet6 2001:2:${fib}::1/64 -alias \
		    > /dev/null 2>&1
		fib=$((fib + 1))
	done
	ipfw delete 10 > /dev/null 2>&1
}

testtx_icmp6_transfernets_gateway()
{

	testtx_ulp6_transfernets_gateway \
	    "testtx_icmp6_transfernets_gateway" "-i"
}


testtx_tcp6_transfernets_gateway()
{

	testtx_ulp6_transfernets_gateway \
	    "testtx_tcp6_transfernets_gateway" ""
}

testtx_udp6_transfernets_gateway()
{

	testtx_ulp6_transfernets_gateway \
	    "testtx_udp6_transfernets_gateway" "-u"
}


################################################################################
#
# RX tests (Remotely originated connections).  The FIB tests happens on peer.
#
#	# For IPFW, IFCONFIG
#	#   For each FIB
#	#     Send OOB well known to work START, wait for reflect
#	#     Send probe, wait for reply with FIB# or RST/ICMP6 unreach
#	#       (in case of ICMP6 use magic like ipfw count and OOB reply)
#	#     Send OOB well known to work DONE, wait for reflect
#	#     Compare real with expected results.
#
testrx_results()
{
	local _r _n _fib i count _instances _transfer _o
	_fib="$1"
	_n="$2"
	_r="$3"
	_instances=$4
	_transfer=$5
	_o="$6"

	print_debug "testrx_results ${_fib} ${_n} ${_r} ${_instances}"

	# Trim "RESULT "
	_r=${_r#* }

	echo "1..${RT_NUMFIBS}"
	while read i count; do
		if test ${_instances} -gt 1; then
			if test ${count} -gt 0; then
				echo "ok ${i} ${_n}result_${i} #" \
				     "FIB ${i} ${count} (tested)"
			else
				echo "not ok ${i} ${_n}result_${i} #" \
				     "FIB ${i} ${count} (tested)"
			fi
		else
			case ${i} in
			${_fib})
				if test ${count} -gt 0; then
					echo "ok ${i} ${_n}result_${i} #" \
					     "FIB ${i} ${count} (tested)"
				else
					echo "not ok ${i} ${_n}result_${i} #" \
					     "FIB ${i} ${count} (tested)"
				fi
				;;
			*)	if test ${count} -eq 0; then
					echo "ok ${i} ${_n}result_${i} #" \
					    "FIB ${i} ${count}"
				else
					echo "not ok ${i} ${_n}result_${i} #" \
					    "FIB ${i} ${count}"
				fi
				;;
			esac
		fi
		i=$((i + 1))
	done <<EOI
`echo "${_r}" | tr ',' '\n'`
EOI

}

testrx_tcp_udp()
{
	local _n _o _f testno i _fibtxt _instances _res _port _transfer _p
	_n="$1"
	_o="$2"
	_f="$3"
	_instances=$4
	_transfer=$5

	# Unused so far.
	: ${_instances:=1}

	printf "1..%d\n" `expr 2 \* ${RT_NUMFIBS}`
	testno=1
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		print_debug "Testing ${_f} ${i}"

		# We are expecting the FIB number (only) to be returned.
		eval _rc="\${rc_${i}_l}"
		_fibtxt="${_n}_${i}_l ${_f} ${i} ${PEERLINKLOCAL}"
		if test ${_instances} -gt 1; then
			_res="FIB ${i}"
			_port=$((CTRLPORT + 1000 + i))
		else
			_res="${_fibtxt}"
			_port=${CTRLPORT}
		fi
		nc_send_recv ${i} 1 "${_fibtxt}" "${_res}" ${PEERLINKLOCAL} \
		    ${_port} "-6 ${_o} -w1"
		check_rc $? ${_rc} ${testno} "${_fibtxt}" "${_reply}"
		testno=$((testno + 1))

		# If doing multiple transfer networks, replace PEERADDR.
		_p="${PEERADDR}"
		case ${_transfer} in
		1)	PEERADDR=2001:2:${i}::2 ;;
		esac

		eval _rc="\${rc_${i}_a}"
		_fibtxt="${_n}_${i}_a ${_f} ${i} ${PEERADDR}"
		if test ${_instances} -gt 1; then
			_res="FIB ${i}"
			_port=$((CTRLPORT + 1000 + i))
		else
			_res="${_fibtxt}"
			_port=${CTRLPORT}
		fi
		nc_send_recv ${i} 1 "${_fibtxt}" "${_res}" ${PEERADDR} \
		    ${_port} "-6 ${_o} -w1"
		check_rc $? ${_rc} ${testno} "${_fibtxt}" "${_reply}"
		testno=$((testno + 1))

		# Restore PEERADDR.
		PEERADDR="${_p}"

		i=$((i + 1))
	done
}


testrx_setup_transfer_networks()
{
	local i

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::1/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 2001:2:${i}::1/64 alias
		i=$((i + 1))
	done
}

testrx_cleanup_transfer_networks()
{
	local i

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::1/64 -alias \
		    > /dev/null 2>&1
		i=$((i + 1))
	done
}


testrx_run_test()
{
	local _n _t _fib _o _txt _msg i _reply _instances _destructive _transfer
	_n="$1"
	_t="$2"
	_fib=$3
	_o="$4"
	_instances=$5
	_detsructive=$6
	_transfer=$7

	# Netcat options (for UDP basically).
	case "${_o}" in
	-i)	_opts="" ;;		# Use TCP for START/DONE.
	*)	_opts="${_o}" ;;
	esac

	# Combined test case base name.
	case ${USE_SOSETFIB} in
	0)	_f="setfib" ;;
	1)	_f="so_setfib" ;;
	*)	die "Unexpected value for SO_SETFIB: ${SO_SETFIB}" ;;
	esac
	_txt="${_n}_${_f}_${_t}_${_fib}_${_instances}_${_detsructive}_${_transfer}"

	print_debug "Starting test '${_txt}' (for ${_instances} instances)."

	case ${_transfer} in
	1)	testrx_setup_transfer_networks ;;
	esac

	# Let the other side a chance to get ready as well.
	sleep 1

	set +e
	# Let peer know that we are about to start.
	_msg="START ${_txt}"
	nc_send_recv ${_fib} ${WAITS} "${_msg}" "${_msg}" ${PEERADDR} \
	    ${CTRLPORT} "-6 ${_opts} -w1"
	case $? in
	0)	;;
	*)	die "Got invalid reply from peer." \
		    "Expected '${_msg}', got '${_reply}'" ;;
	esac

	# Let the other side a chance to get ready as well.
	sleep 1

	# Send probe.
	case "${_o}" in
	-i)	testtx_icmp6 "${_txt}_" ${_transfer} ;;
	*)	testrx_tcp_udp "${_txt}" "${_o}" "${_fib}" ${_instances} \
		    ${_transfer} ;;
	esac

	# Let peer know that we are done with this test to move to next.
	# This must immediately succeed.
	_msg="DONE ${_txt}"
	nc_send_recv ${_fib} ${WAITS} "${_msg}" "${_msg}" ${PEERADDR} \
	    ${CTRLPORT} "-6 ${_opts} -w1"
	case $? in
	0)	;;
	*)	die "Got invalid reply from peer." \
		    "Expected '${_msg}', got '${_reply}'" ;;
	esac

	# Collect and validate the results.   Always use TCP.
	sleep 1
	set +e
	nc_send_recv ${_fib} 1 "RESULT REQUEST" "" ${PEERADDR} \
	    ${CTRLPORT} "-6 -w1"
	case "${_reply}" in
	RESULT\ *) testrx_results ${_fib} "${_txt}_" "${_reply}" ${_instances} \
			${_transfer} "${_o}"
		;;
	*)	die "Got invalid reply from peer." \
		    "Expected 'RESULT ...', got '${_reply}'" ;;
	esac
	set -e

	case ${_transfer} in
	1)	testrx_cleanup_transfer_networks ;;
	esac

	print_debug "Successfully received status update '${_reply}'."
}

testrx_main_setup_rc()
{
	local _n _t _fib _o _instances _destructive _transfer i
	_n="$1"
	_t="$2"
	_fib=$3
	_o="$4"
	_instances=$5
	_destructive=$6
	_transfer=$7

	# Setup expected return values.
	if test ${_destructive} -eq 0; then
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			eval rc_${i}_l=0
			eval rc_${i}_a=0
			i=$((i + 1))
		done
	else
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			eval rc_${i}_l=0
			case ${i} in
			${_fib})	eval rc_${i}_a=0 ;;
			*)	# ICMP6 cannot be distinguished and will
				# always work in single transfer network.
				case "${_o}" in
				-i)	case ${_transfer} in
					0) eval rc_${i}_a=0 ;;
					1) eval rc_${i}_a=1 ;;
					esac
					;;
				*)	if test ${_instances} -eq 1 -a \
					    ${_transfer} -eq 0; then
						eval rc_${i}_a=0
					else
						eval rc_${i}_a=1
					fi
					;;
				esac
				;;
			esac
			i=$((i + 1))
		done
	fi

	print_debug "${_n}_${t}_${_fib} ${_instances} ${_destructive}" \
	    "${_transfer}"
	testrx_run_test "${_n}" "${t}" ${_fib} "${_o}" ${_instances} \
	    ${_destructive} ${_transfer}
}

testrx_main()
{
	local _n _o s t fib _instances _destructive _transfer
	_n="$1"
	_o="$2"
	_instances=$3

	: ${_instances:=1}

	print_debug "${_n}"
	for _transfer in 1 0; do
		for _destructive in 0 1; do
			for t in ipfw ifconfig; do

				print_debug "${_n}_${t}"
				fib=0
				while test ${fib} -lt ${RT_NUMFIBS}; do

					testrx_main_setup_rc "${_n}" "${t}" \
					    ${fib} "${_o}" ${_instances} \
					    ${_destructive} ${_transfer}

					fib=$((fib + 1))
				done
			done
		done
	done
}

################################################################################
#
#
#

testrx_icmp6_same_addr_one_fib_a_time()
{

	testrx_main \
	    "testrx_icmp6_same_addr_one_fib_a_time" "-i"
}

testrx_tcp6_same_addr_one_fib_a_time()
{

	testrx_main \
	    "testrx_tcp6_same_addr_one_fib_a_time" ""
}


testrx_udp6_same_addr_one_fib_a_time()
{

	testrx_main \
	    "testrx_udp6_same_addr_one_fib_a_time" "-u"
}


################################################################################

testrx_tcp6_same_addr_all_fibs_a_time()
{

	testrx_main \
	    "testrx_tcp6_same_addr_all_fibs_a_time" "" ${RT_NUMFIBS}
}

testrx_udp6_same_addr_all_fibs_a_time()
{

	testrx_main \
	    "testrx_udp6_same_addr_all_fibs_a_time" "-u" ${RT_NUMFIBS}
}


################################################################################
#
# Prereqs.
#
if test `sysctl -n security.jail.jailed` -eq 0; then
	kldload ipfw > /dev/null 2>&1 || kldstat -v | grep -q ipfw 

	# Reduce the time we wait in case of no reply to 2s.
	sysctl net.inet.tcp.keepinit=2000 > /dev/null 2>&1
fi
ipfw -f flush > /dev/null 2>&1 || die "please load ipfw in base system"
ipfw add 65000 permit ip from any to any > /dev/null 2>&1

################################################################################
#
# Run tests.
#

# 64 cases at 16 FIBs.
check_local_addr
check_local_tun

send_greeting

# Initiator testing.
for uso in 0 1; do

	USE_SOSETFIB=${uso}

	# Only run ICMP6 tests for the first loop.
	# 160 cases at 16 FIBs.
	test ${uso} -ne 0 || testtx_icmp6_connected && sleep 1
	testtx_tcp6_connected && sleep 1
	testtx_udp6_connected && sleep 1

	# 2560 cases at 16 FIBs.
	test ${uso} -ne 0 || testtx_icmp6_connected_blackhole && sleep 1
	testtx_tcp6_connected_blackhole && sleep 1
	testtx_udp6_connected_blackhole && sleep 1

	# 2560 cases at 16 FIBs.
	test ${uso} -ne 0 || testtx_icmp6_connected_transfernets && sleep 1
	testtx_tcp6_connected_transfernets && sleep 1
	testtx_udp6_connected_transfernets && sleep 1

	# 2560 cases at 16 FIBs.
	test ${uso} -ne 0 || \
	    testtx_icmp6_connected_ifconfig_transfernets && sleep 1
	testtx_tcp6_connected_ifconfig_transfernets && sleep 1
	testtx_udp6_connected_ifconfig_transfernets && sleep 1

	# 160 cases at 16 FIBs.
	test ${uso} -ne 0 || testtx_icmp6_gateway && sleep 1
	testtx_tcp6_gateway && sleep 1
	testtx_udp6_gateway && sleep 1

	# 160 cases at 16 FIBs.
	test ${uso} -ne 0 || testtx_icmp6_transfernets_gateways && sleep 1
	testtx_tcp6_transfernets_gateways && sleep 1
	testtx_udp6_transfernets_gateways && sleep 1

	# 2560 cases at 16 FIBs.
	test ${uso} -ne 0 || testtx_icmp6_transfernets_gateway && sleep 1
	testtx_tcp6_transfernets_gateway && sleep 1
	testtx_udp6_transfernets_gateway && sleep 1
done

# Receiver testing.
for uso in 0 1; do

	USE_SOSETFIB=${uso}

	# Only expect ICMP6 tests for the first loop.
	# 6144 cases at 16 FIBs.
	test ${uso} -ne 0 || testrx_icmp6_same_addr_one_fib_a_time && sleep 1
	# 12288 cases at 16 FIBs.
	testrx_tcp6_same_addr_one_fib_a_time && sleep 1
	# 12288 cases at 16 FIBs.
	testrx_udp6_same_addr_one_fib_a_time && sleep 1

	# 12288 cases at 16 FIBs.
	testrx_tcp6_same_addr_all_fibs_a_time && sleep 1
	# 12288 cases at 16 FIBs.
	testrx_udp6_same_addr_all_fibs_a_time && sleep 1

done

cleanup

# end
