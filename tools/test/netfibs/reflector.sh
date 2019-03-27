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

# Control port we use to exchange messages between nodes to sync. tests, etc.
: ${CTRLPORT:=6666}

# Get the number of FIBs from the kernel.
RT_NUMFIBS=`sysctl -n net.fibs`

PEERADDR="2001:2:ff00::1"
OURADDR="2001:2:ff00::2"

OURLINKLOCAL=""
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

# Function to avoid prelist races adding and deleting prefixes too quickly.
delay()
{

	# sleep 1 is too long.
	touch /tmp/foo || true
	stat /tmp/foo > /dev/null 2>&1 || true
}

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
	echo "${_r} ${_testno} ${_testname} # ${_msg} ${_rc}"
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

# Setup our side and wait for the peer to tell us that it is ready.
wait_remote_ready()
{
	local _greeting _keyword _fibs _linklocal i

	print_debug "Setting up interface ${IFACE}"
	ifconfig ${IFACE} inet6 ${OURADDR}/64 -alias > /dev/null 2>&1 || true
	delay
	ifconfig ${IFACE} inet6 ${OURADDR}/64 alias up
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 -alias \
		    > /dev/null 2>&1 || true
		delay
		i=$((i + 1))
	done
	OURLINKLOCAL=`ifconfig ${IFACE} | awk '/inet6 fe80:/ { print $2 }'`

	# Let things settle.
	print_debug "Waiting 4 seconds for things to settle"
	sleep 4

	# Wait for the remote to connect and start things.
	# We tell it the magic keyword, our number of FIBs and our link-local.
	# It already knows our global address.
	_greeting=`echo "SETUP ${RT_NUMFIBS} ${OURLINKLOCAL}" | \
	    nc -6 -l ${CTRLPORT}`

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

	print_debug "Successfully received greeting. Peer at ${PEERLINKLOCAL}"
}

cleanup()
{

	print_debug "Removing address from interface ${IFACE}"
	ifconfig ${IFACE} inet6 ${OURADDR}/64 -alias
	delay
}

################################################################################
#
testtx_icmp6_connected()
{
	local _opts

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac
	print_debug "./reflect -p ${CTRLPORT} -T TCP6 " \
	    "-t testtx_icmp6_connected ${_opts}"
	./reflect -p ${CTRLPORT} -T TCP6 -t testtx_icmp6_connected ${_opts}
	print_debug "reflect terminated without error."
}

testtx_tcp6_connected()
{
	local _opts

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac
	print_debug "./reflect -p ${CTRLPORT} -T TCP6 " \
	    "-t testtx_tcp6_connected ${_opts}"
	./reflect -p ${CTRLPORT} -T TCP6 -t testtx_tcp6_connected ${_opts}
	print_debug "reflect terminated without error."
}

testtx_udp6_connected()
{
	local _opts

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac
	print_debug "./reflect -p ${CTRLPORT} -T UDP6 " \
	    "-t testtx_udp6_connected ${_opts}"
	./reflect -p ${CTRLPORT} -T UDP6 -t testtx_udp6_connected ${_opts}
	print_debug "reflect terminated without error."
}

################################################################################
#
testtx_icmp6_connected_blackhole()
{
	local _opts fib

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		print_debug "./reflect -p ${CTRLPORT} -T TCP6 " \
		    "-t testtx_icmp6_connected_blackhole${fib} ${_opts}"
		./reflect -p ${CTRLPORT} -T TCP6 \
		    -t testtx_icmp6_connected_blackhole${fib} ${_opts}
		print_debug "reflect terminated without error."
		fib=$((fib + 1))
	done
}

testtx_tcp6_connected_blackhole()
{
	local _opts fib

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		print_debug "./reflect -p ${CTRLPORT} -T TCP6 " \
		    "-t testtx_tcp6_connected_blackhole${fib} ${_opts}"
		./reflect -p ${CTRLPORT} -T TCP6 \
		    -t testtx_tcp6_connected_blackhole${fib} ${_opts}
		print_debug "reflect terminated without error."
		fib=$((fib + 1))
	done
}

testtx_udp6_connected_blackhole()
{
	local _opts fib

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		print_debug "./reflect -p ${CTRLPORT} -T UDP6 " \
		    "-t testtx_udp6_connected_blackhole${fib} ${_opts}"
		./reflect -p ${CTRLPORT} -T UDP6 \
		    -t testtx_udp6_connected_blackhole${fib} ${_opts}
		print_debug "reflect terminated without error."
		fib=$((fib + 1))
	done
}

################################################################################
#
testtx_ulp6_connected_transfernets()
{
	local _opts fib _n _o
	_n="$1"
	_o="$2"

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	# Setup transfer networks.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} \
		    ifconfig ${IFACE} inet6 2001:2:${fib}::2/64 alias
		fib=$((fib + 1))
	done

	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		print_debug "./reflect -p ${CTRLPORT} -T ${_o} -t ${_n}${fib} ${_opts}"
		./reflect -p ${CTRLPORT} -T ${_o} -t ${_n}${fib} ${_opts}
		print_debug "reflect terminated without error."
		fib=$((fib + 1))
	done

	# Cleanup transfer networks.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} \
		    ifconfig ${IFACE} inet6 2001:2:${fib}::2/64 -alias
		delay
		fib=$((fib + 1))
	done
}

testtx_icmp6_connected_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_icmp6_connected_transfernets" "TCP6"
}

testtx_tcp6_connected_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_tcp6_connected_transfernets" "TCP6"
}

testtx_udp6_connected_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_udp6_connected_transfernets" "UDP6"
}

testtx_icmp6_connected_ifconfig_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_icmp6_connected_ifconfig_transfernets" "TCP6"
}

testtx_tcp6_connected_ifconfig_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_tcp6_connected_ifconfig_transfernets" "TCP6"
}

testtx_udp6_connected_ifconfig_transfernets()
{

	testtx_ulp6_connected_transfernets \
	    "testtx_udp6_connected_ifconfig_transfernets" "UDP6"
}

################################################################################
#
testtx_ulp6_gateway()
{
	local _opts _n _o
	_n="$1"
	_o="$2"

	ifconfig lo0 inet6 2001:2:ff01::2 -alias > /dev/null 2>&1 || true
	delay
	ifconfig lo0 inet6 2001:2:ff01::2 alias

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	print_debug "./reflect -p ${CTRLPORT} -T ${_o} " \
	    "-t ${_n} ${_opts} -A 2001:2:ff01::2"
	./reflect -p ${CTRLPORT} -T ${_o} \
	    -t ${_n} ${_opts} -A 2001:2:ff01::2
	print_debug "reflect terminated without error."

	ifconfig lo0 inet6 2001:2:ff01::2 -alias
	delay
}

testtx_icmp6_gateway()
{

	testtx_ulp6_gateway "testtx_icmp6_gateway" "TCP6"
}

testtx_tcp6_gateway()
{

	testtx_ulp6_gateway "testtx_tcp6_gateway" "TCP6"
}

testtx_udp6_gateway()
{

	testtx_ulp6_gateway "testtx_udp6_gateway" "UDP6"
}

################################################################################
#
testtx_ulp6_transfernets_gateways()
{
	local _opts fib _n _o
	_n="$1"
	_o="$2"

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	# Setup transfer networks.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} \
		    ifconfig ${IFACE} inet6 2001:2:${fib}::2/64 alias
		fib=$((fib + 1))
	done

	# Setup out listener IP.
	ifconfig lo0 inet6 2001:2:ff01::2 -alias > /dev/null 2>&1 || true
	delay
	ifconfig lo0 inet6 2001:2:ff01::2 alias

	# Reflect requests.
	print_debug "./reflect -p ${CTRLPORT} -T ${_o} " \
	    "-t ${_n} ${_opts} -A 2001:2:ff01::2"
	./reflect -p ${CTRLPORT} -T ${_o} \
	    -t ${_n} ${_opts} -A 2001:2:ff01::2
	print_debug "reflect terminated without error."

	# Cleanup transfer networks and listener IP.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} \
		    ifconfig ${IFACE} inet6 2001:2:${fib}::2/64 -alias
		delay
		fib=$((fib + 1))
	done
	ifconfig lo0 inet6 2001:2:ff01::2 -alias
}

testtx_icmp6_transfernets_gateways()
{

	testtx_ulp6_transfernets_gateways \
	    "testtx_icmp6_transfernets_gateways" "TCP6"
}

testtx_tcp6_transfernets_gateways()
{

	testtx_ulp6_transfernets_gateways \
	    "testtx_tcp6_transfernets_gateways" "TCP6"
}

testtx_udp6_transfernets_gateways()
{

	testtx_ulp6_transfernets_gateways \
	    "testtx_udp6_transfernets_gateways" "UDP6"
}


################################################################################
#
testtx_ulp6_transfernets_gateway()
{
	local _opts fib _n _o
	_n="$1"
	_o="$2"

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	# Setup transfer networks.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} \
		    ifconfig ${IFACE} inet6 2001:2:${fib}::2/64 alias
		fib=$((fib + 1))
	done

	# Setup out listener IP.
	ifconfig lo0 inet6 2001:2:ff01::2 -alias > /dev/null 2>&1 || true
	delay
	ifconfig lo0 inet6 2001:2:ff01::2 alias

	# Reflect requests.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		print_debug "./reflect -p ${CTRLPORT} -T ${_o} " \
		    "-t ${_n}${fib} ${_opts} -A 2001:2:ff01::2"
		./reflect -p ${CTRLPORT} -T ${_o} \
		    -t ${_n}${fib} ${_opts} -A 2001:2:ff01::2
		print_debug "reflect terminated without error."
		fib=$((fib + 1))
	done

	# Cleanup transfer networks and listener IP.
	fib=0
	while test ${fib} -lt ${RT_NUMFIBS}; do
		setfib -F${fib} \
		    ifconfig ${IFACE} inet6 2001:2:${fib}::2/64 -alias
		delay
		fib=$((fib + 1))
	done
	ifconfig lo0 inet6 2001:2:ff01::2 -alias
}

testtx_icmp6_transfernets_gateway()
{

	testtx_ulp6_transfernets_gateway \
	    "testtx_icmp6_transfernets_gateway" "TCP6"
}

testtx_tcp6_transfernets_gateway()
{

	testtx_ulp6_transfernets_gateway \
	    "testtx_tcp6_transfernets_gateway" "TCP6"
}

testtx_udp6_transfernets_gateway()
{

	testtx_ulp6_transfernets_gateway \
	    "testtx_udp6_transfernets_gateway" "UDP6"
}

################################################################################
#
# We are receiver, but the FIBs are with us this time.
#
#

#       # For IPFW, IFCONFIG
#       #   For each FIB
#       #     Send OOB well known to work START, wait for reflect
#       #     Send probe, wait for reply with FIB# or RST/ICMP6 unreach
#       #       (in case of ICMP6 use magic like ipfw count and OOB reply)
#       #     Send OOB well known to work DONE, wait for reflect   
#       #     Compare real with expected results.
#

textrx_ipfw_setup()
{
	local _fib _transfer i _p _o
	_fib=$1
	_transfer=$2

	# ICMP6 would need content inspection to distinguish FIB, we can
	# only differentiate by address.
	# For the default single-address cases always set to current FIB.
	ipfw add 100 setfib ${_fib} ipv6-icmp \
	    from ${PEERADDR} to ${OURADDR} \
	    via ${IFACE} in > /dev/null 2>&1
	ipfw add 100 setfib ${_fib} ipv6-icmp \
	    from ${PEERLINKLOCAL%\%*} to ${OURLINKLOCAL%\%*} \
	    via ${IFACE} in > /dev/null 2>&1

	# Always also do a setfib for the control port so that OOB
	# signaling workes even if we remove connected subnets.
	ipfw add 200 setfib ${_fib} ip6 from ${PEERADDR} to ${OURADDR} \
	    dst-port ${CTRLPORT} via ${IFACE} in > /dev/null 2>&1

	# Save addresses
	_p="${PEERADDR}"
	_o="${OURADDR}"

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do

		# If doing multiple transfer networks, replace PEERADDR.
		case ${_transfer} in
		1)	PEERADDR=2001:2:${i}::1
			OURADDR=2001:2:${i}::2
			;;
		esac

		if test ${_instances} -gt 1 -o ${_transfer} -eq 1; then
			ipfw add 400 setfib ${_fib} ipv6-icmp \
			    from ${PEERADDR} to ${OURADDR} \
			    icmp6types 128 \
			    via ${IFACE} in > /dev/null 2>&1
		fi
		
		case ${i} in
		${_fib})
			ipfw add 400 setfib ${_fib} ip6 \
			    from ${PEERADDR} to ${OURADDR} \
			    dst-port $((CTRLPORT + 1000 + i)) \
			    via ${IFACE} in > /dev/null 2>&1
			ipfw add 400 setfib ${_fib} ip6 \
			    from ${PEERLINKLOCAL%\%*} to ${OURLINKLOCAL%\%*} \
			    dst-port $((CTRLPORT + 1000 + i)) \
			    via ${IFACE} in > /dev/null 2>&1
			if test ${_instances} -le 1 -o ${_transfer} -ne 1; then
				ipfw add 400 setfib ${_fib} ipv6-icmp \
				    from ${PEERADDR} to ${OURADDR} \
				    icmp6types 128 \
				    via ${IFACE} in > /dev/null 2>&1
			fi
			;;
		esac

		i=$((i + 1))
	done

	# Restore addresses.
	PEERADDR="${_p}"
	OURADDR="${_o}"

	case ${DEBUG} in
	''|0)	;;
	*)	ipfw show ;;
	esac
}

textrx_ifconfig_setup()
{
	local _fib
	_fib=$1

	ifconfig ${IFACE} fib ${_fib} > /dev/null 2>&1
}

textrx_ipfw_cleanup()
{
	local i

	case ${DEBUG} in
	''|0)	;;
	*)	ipfw show ;;
	esac

	ipfw delete 100 > /dev/null 2>&1 || true
	ipfw delete 200 > /dev/null 2>&1 || true
	ipfw delete 400 > /dev/null 2>&1 || true

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do

		ipfw delete $((1000 + i)) > /dev/null 2>&1 || true
		i=$((i + 1))
	done
}

textrx_ifconfig_cleanup()
{

	ifconfig ${IFACE} fib 0 > /dev/null 2>&1
}

textrx_count_setup()
{
	local i

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do

		# Count ICMP6 echo replies.
		ipfw add $((500 + i)) count ipv6-icmp from any to any \
		    icmp6types 129 fib ${i} via ${IFACE} out > /dev/null 2>&1
		ipfw add $((500 + i)) count tcp from any to any \
		    fib ${i} via ${IFACE} out > /dev/null 2>&1
		ipfw add $((500 + i)) count udp from any to any \
		    fib ${i} via ${IFACE} out > /dev/null 2>&1
		i=$((i + 1))
	done
}

textrx_count_results()
{
	local _fib _o i _rstr _c _req _p _opts
	_fib=$1
	_o="$2"

	case ${DEBUG} in
	''|0)	;;
	*)	ipfw show ;;
	esac

	_rstr=""
	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do

		case "${_o}" in
		"-i")	_c=`ipfw show $((500 + i)) | awk '/ ipv6-icmp / { print $2 }'` ;;
		"-u")	_c=`ipfw show $((500 + i)) | awk '/ udp / { print $2 }'` ;;
		*)	_c=`ipfw show $((500 + i)) | awk '/ tcp / { print $2 }'` ;;
		esac
		_rstr="${_rstr}${i} ${_c},"

		ipfw delete $((500 + i)) > /dev/null 2>&1 || true
		i=$((i + 1))
	done

	# We do not care about the request.
	_req=`echo "RESULT ${_rstr}" | nc -V ${_fib} -6 -l ${CTRLPORT}`
	print_debug "$? -- ${_req} -- RESULT ${_rstr}"
}

testrx_remove_connected()
{
	local _fib _transfer i j _prefix
	_fib=$1
	_transfer=$2

	if test ${_transfer} -eq 1; then
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			j=0
			while test ${j} -lt ${RT_NUMFIBS}; do
				_prefix="2001:2:${j}::"

				case ${j} in
				${_fib});;
				*)	print_debug "setfib -F${i} route delete" \
					    "-inet6 -net ${_prefix}"
					setfib -F${i} route delete -inet6 -net \
					    ${_prefix} > /dev/null 2>&1
					;;
				esac
				j=$((j + 1))
			done
			i=$((i + 1))
		done

	else
		_prefix=${OURADDR%2}	# Luckily we know the details.
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do

			case ${i} in
			${_fib});;
			*)	print_debug "setfib -F${i} route delete" \
				    "-inet6 -net ${_prefix}"
				setfib -F${i} route delete -inet6 -net \
				    ${_prefix} > /dev/null 2>&1
				;;
			esac

			i=$((i + 1))
		done
	fi
}

testrx_cleanup_connected()
{
	local _fib _transfer i _prefix
	_fib=$1
	_transfer=$2

	if test ${_transfer} -eq 1; then

		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			setfib -F${i} \
			   ifconfig ${IFACE} inet6 2001:2:${i}::2/64 -alias \
			    > /dev/null 2>&1
			delay
			i=$((i + 1))
		done

	else
		# Use the hammer removing the address and adding it again to get
		# the connected subnet back to all FIBs.  Hard to do otherwise.
		ifconfig ${IFACE} inet6 ${OURADDR}/64 -alias || true
		delay
		ifconfig ${IFACE} inet6 ${OURADDR}/64 alias up
	fi
}

testrx_setup_transfer_networks()
{
	local i

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 -alias \
		    > /dev/null 2>&1 || true
		delay
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 alias
		i=$((i + 1))
	done
}

testrx_run_one()
{
	local _fib _txt _opts
	_fib=$1
	_txt="$2"
	_opts="$3"

	case ${USE_SOSETFIB} in
	0)	print_debug "setfib -F${_fib} ./reflect -p ${CTRLPORT}" \
		    "-t ${_txt} ${_opts}"
		setfib -F${_fib} ./reflect -p ${CTRLPORT} -t ${_txt} ${_opts}
		;;
	1)	print_debug "./reflect -F${_fib} -p ${CTRLPORT} -t ${_txt}" \
		    "${_opts}"
		./reflect -F${_fib} -p ${CTRLPORT} -t ${_txt} ${_opts}
		;;
	*)	die "Invalid value for USE_SOSETFIB: ${USE_SOSETFIB}" ;;
	esac
	print_debug "reflect '${_txt}' terminated without error."
}

testrx_run_multiple()
{
	local _fib _txt _opts i _jobs _p _w
	_fib=$1
	_txt="$2"
	_opts="$3"

	i=0
	_jobs=""
	while test ${i} -lt ${RT_NUMFIBS}; do
		case ${USE_SOSETFIB} in
		0)	print_debug "setfib -F${i} ./reflect" \
			    "-p $((CTRLPORT + 1000 + i))" \
			    "-t ${_txt} ${_opts} -N -f ${i} &"
			setfib -F${i} ./reflect -p $((CTRLPORT + 1000 + i)) \
			    -t ${_txt} ${_opts} -N -f ${i} &
			;;
		1)	print_debug "./reflect -F ${i}" \
			    "-p $((CTRLPORT + 1000 + i))" \
			    "-t ${_txt} ${_opts} -N -f ${i} &"
			./reflect -F ${i} -p $((CTRLPORT + 1000 + i)) \
			    -t ${_txt} ${_opts} -N -f ${i} &
			;;
		*)	die "Invalid value for USE_SOSETFIB: ${USE_SOSETFIB}" ;;
		esac
		_p=$!
		_jobs="${_jobs}${_p} "
		case ${i} in
		${_fib}) _w=${_p} ;;
		esac
		i=$((i + 1))
	done

	# Start OOB control connection for START/DONE.
	testrx_run_one ${_fib} "${_txt}" "${_opts}"
	print_debug "KILL ${_jobs}"
	for i in ${_jobs}; do
		kill ${i} || true
	done
	#killall reflect || true
	print_debug "reflects for '${_txt}' terminated without error."
}

testrx_run_test()
{
	local _n _t _fib _o _txt i _f _instance _destructive _transfer
	_n="$1"
	_t="$2"
	_fib=$3
	_o="$4"
	_instances=$5
	_destructive=$6
	_transfer=$7

	: ${_destructive:=0}

	_opts=""
	case ${DEBUG} in
	''|0)	;;
	42)	_opts="-d -d" ;;
	*)	_opts="-d" ;;
	esac

	# Convert netcat options to reflect aguments.
	case "${_o}" in
	-i)	_opts="${_opts} -T TCP6" ;;	# Use TCP for START/DONE.
	-u)	_opts="${_opts} -T UDP6" ;;
	*)	_opts="${_opts} -T TCP6" ;;
	esac

	# Combined test case base name.
	case ${USE_SOSETFIB} in
	0)	_f="setfib" ;;
	1)	_f="so_setfib" ;;
	*)	die "Unexpected value for SO_SETFIB: ${SO_SETFIB}" ;;
	esac

        _txt="${_n}_${_f}_${_t}_${_fib}_${_instances}_${_destructive}_${_transfer}"

	case ${_transfer} in
	1)	testrx_setup_transfer_networks ;;
	esac

	case "${_t}" in
	ipfw)		textrx_ipfw_setup ${_fib} ${_transfer} ${_instances} ;;
	ifconfig)	textrx_ifconfig_setup ${_fib} ;;
	*)		die "Invalid type in ${_txt}" ;;
	esac

	# Setup unresponsive FIBs.
	case ${_destructive} in
	1)	testrx_remove_connected ${_fib} ${_transfer} ;;
	esac

	# Setup to get result counts.
	textrx_count_setup

	# Run just one / one per FIB (with incremental ports).
	#case ${_instances} in
	#1)	testrx_run_one ${_fib} "${_txt}" "${_opts}" ;;
	#*)	testrx_run_multiple ${_fib} "${_txt}" "${_opts}" ;;
	#esac
	testrx_run_multiple ${_fib} "${_txt}" "${_opts}" ${_transfer}

	# Export result counts.
	textrx_count_results ${_fib} "${_o}"

	# Cleanup unresponsive  FIBs or multiple prefixes.
	if test ${_destructive} -eq 1 -o ${_transfer} -eq 1; then
		testrx_cleanup_connected ${_fib} ${_transfer}
	fi

	case "${_t}" in
	ipfw)		textrx_ipfw_cleanup ;;
	ifconfig)	textrx_ifconfig_cleanup ;;
	*)		die "Invalid type in ${_txt}" ;;
	esac
}

testrx_main()
{
	local _n _o s t fib _instances _destructive
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

					print_debug "${_n}_${t}_${fib}" \
					    "${_instances} ${_destructive}" \
					    "${_transfer}"
					testrx_run_test "${_n}" "${t}" ${fib} \
					   "${_o}" ${_instances} \
					   ${_destructive} ${_transfer}

					fib=$((fib + 1))
				done
			done
		done
	done
}

################################################################################
#
# Probe all FIBs with one "active" one a time.
#
testrx_icmp6_same_addr_one_fib_a_time()
{

	testrx_main "testrx_icmp6_same_addr_one_fib_a_time" "-i"
}

testrx_tcp6_same_addr_one_fib_a_time()
{

	testrx_main "testrx_tcp6_same_addr_one_fib_a_time" ""
}

testrx_udp6_same_addr_one_fib_a_time()
{

	testrx_main "testrx_udp6_same_addr_one_fib_a_time" "-u"
}

################################################################################
#
# Probe all FIBs with all "active" all time.
#
testrx_tcp6_same_addr_all_fibs_a_time()
{

	testrx_main "testrx_tcp6_same_addr_all_fibs_a_time" "" ${RT_NUMFIBS}
}

testrx_udp6_same_addr_all_fibs_a_time()
{

	testrx_main "testrx_udp6_same_addr_all_fibs_a_time" "-u" ${RT_NUMFIBS}
}


################################################################################
#
# Prereqs.
#
if test `sysctl -n security.jail.jailed` -eq 0; then
	kldload ipfw > /dev/null 2>&1 || kldstat -v | grep -q ipfw 
fi
ipfw -f flush > /dev/null 2>&1 || die "please load ipfw in base system"
ipfw add 65000 permit ip from any to any > /dev/null 2>&1
killall reflect || true

################################################################################
#
# Run tests.
#
wait_remote_ready

# We are receiver reflecting the input back.
for uso in 0 1; do

	# Only run ICMP6 tests for the first loop.
	test ${uso} -ne 0 || testtx_icmp6_connected
	testtx_tcp6_connected
	testtx_udp6_connected

	test ${uso} -ne 0 || testtx_icmp6_connected_blackhole
	testtx_tcp6_connected_blackhole
	testtx_udp6_connected_blackhole

	test ${uso} -ne 0 || testtx_icmp6_connected_transfernets
	testtx_tcp6_connected_transfernets
	testtx_udp6_connected_transfernets

	test ${uso} -ne 0 || testtx_icmp6_connected_ifconfig_transfernets
	testtx_tcp6_connected_ifconfig_transfernets
	testtx_udp6_connected_ifconfig_transfernets

	test ${uso} -ne 0 || testtx_icmp6_gateway
	testtx_tcp6_gateway
	testtx_udp6_gateway

	test ${uso} -ne 0 || testtx_icmp6_transfernets_gateways
	testtx_tcp6_transfernets_gateways
	testtx_udp6_transfernets_gateways

	test ${uso} -ne 0 || testtx_icmp6_transfernets_gateway
	testtx_tcp6_transfernets_gateway
	testtx_udp6_transfernets_gateway
done

ipfw -f flush > /dev/null 2>&1
ipfw add 65000 permit ip from any to any > /dev/null 2>&1

# We are receiver, but the FIBs are with us this time.
for uso in 0 1; do

	USE_SOSETFIB=${uso}
	
	# Only expect ICMP6 tests for the first loop.
	test ${uso} -ne 0 || testrx_icmp6_same_addr_one_fib_a_time
	testrx_tcp6_same_addr_one_fib_a_time
	testrx_udp6_same_addr_one_fib_a_time

	testrx_tcp6_same_addr_all_fibs_a_time
	testrx_udp6_same_addr_all_fibs_a_time

done

cleanup

# end
