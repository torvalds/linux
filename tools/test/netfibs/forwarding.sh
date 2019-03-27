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

# Test setup:
#
#  left ------------------------- middle ------------------------- right
#    IFACE                     IFACE  IFACEFAR                  IFACE
#    LEFTADDR         MIDDLELEFTADDR  MIDDLERIGHTADDR       RIGHTADDR
#                               forwarding=1
#    initiator                   FIB tests                  reflector

# We will use the RFC5180 (and Errata) benchmarking working group prefix
# 2001:0002::/48 for testing.
PREFIX="2001:2:"

# Set IFACE to the real interface you want to run the test on.
# IFACEFAR is only relevant on the middle (forwarding) node and will be the
# 'right' side (far end) one.
: ${IFACE:=lo0}
: ${IFACEFAR:=lo0}

# Number of seconds to wait for peer node to synchronize for test.
: ${WAITS:=120}

# Control port we use to exchange messages between nodes to sync. tests, etc.
: ${CTRLPORT:=6666}

# Get the number of FIBs from the kernel.
RT_NUMFIBS=`sysctl -n net.fibs`

# This is the initiator and connected middle node.
LEFTADDR="2001:2:fe00::1"
MIDDLELEFTADDR="2001:2:fe00::2"
# This is the far end middle node and receiver side.
MIDDLERIGHTADDR="2001:2:ff00::1"
RIGHTADDR="2001:2:ff00::2"

# By default all commands must succeed.  Individual tests may disable this
# temporary.
set -e

# Debug magic.
case "${DEBUG}" in
42)	set -x ;;
esac


################################################################################
#
# Input validation.
#

node=$1
case ${node} in
left)	;;
middle)	;;
right)	;;
*)	echo "ERROR: invalid node name '${node}'. Must be left, middle or" \
	    " right" >&1
	exit 1
	;;
esac

################################################################################
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


################################################################################
#
# Functions to configure networking and do a basic reachability check.
#

setup_networking()
{

	print_debug "Setting up networking"
	case ${node} in
	left)	ifconfig ${IFACE} inet6 ${LEFTADDR}/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 ${LEFTADDR}/64 alias up
		ifconfig ${IFACE} fib 0
		sysctl net.inet6.ip6.forwarding=0 > /dev/null
		route delete -net -inet6 default > /dev/null 2>&1 || true
		route delete -host -inet6 ${RIGHTADDR} ${MIDDLELEFTADDR} \
		    > /dev/null 2>&1 || true
		route add -host -inet6 ${RIGHTADDR} ${MIDDLELEFTADDR} \
		    > /dev/null
		route delete -host -inet6 ${MIDDLERIGHTADDR} ${MIDDLELEFTADDR} \
		    > /dev/null 2>&1 || true
		route add -host -inet6 ${MIDDLERIGHTADDR} ${MIDDLELEFTADDR} \
		    > /dev/null 2>&1 || true
		;;
	middle)	ifconfig ${IFACE} inet6 ${MIDDLELEFTADDR}/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 ${MIDDLELEFTADDR}/64 alias up
		ifconfig ${IFACE} fib 0
		ifconfig ${IFACEFAR} inet6 ${MIDDLERIGHTADDR}/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACEFAR} inet6 ${MIDDLERIGHTADDR}/64 alias up
		ifconfig ${IFACEFAR} fib 0
		sysctl net.inet6.ip6.forwarding=1 > /dev/null
		;;
	right)	ifconfig ${IFACE} inet6 ${RIGHTADDR}/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 ${RIGHTADDR}/64 alias up
		ifconfig ${IFACE} fib 0
		sysctl net.inet6.ip6.forwarding=0 > /dev/null
		route delete -net -inet6 default > /dev/null 2>&1 || true
		route delete -host -inet6 ${LEFTADDR} ${MIDDLERIGHTADDR} \
		    > /dev/null 2>&1 || true
		route add -host -inet6 ${LEFTADDR} ${MIDDLERIGHTADDR} \
		    > /dev/null
		route delete -host -inet6 ${MIDDLELEFTADDR} ${MIDDLERIGHTADDR} \
		    > /dev/null 2>&1 || true
		route add -host -inet6 ${MIDDLELEFTADDR} ${MIDDLERIGHTADDR} \
		    > /dev/null
		;;
	esac

	# Let things settle.
	print_debug "Waiting 4 seconds for things to settle"
	sleep 4
}

cleanup_networking()
{

	case ${node} in
	left)	ifconfig ${IFACE} inet6 ${LEFTADDR}/64 -alias
		;;
	middle)	ifconfig ${IFACE} inet6 ${MIDDLELEFTADDR}/64 -alias
		ifconfig ${IFACEFAR} inet6 ${MIDDLERIGHTADDR}/64 -alias
		sysctl net.inet6.ip6.forwarding=0 > /dev/null
		;;
	right)	ifconfig ${IFACE} inet6 ${RIGHTADDR}/64 -alias
		;;
	esac
	print_debug "Cleaned up networking"
}

_reachability_check()
{
	local _addr _rc
	_addr="$1"

	ping6 -n -c1 ${_addr} > /dev/null 2>&1
	_rc=$?
	case ${_rc} in
	0)	;;
	*)	print_debug "cannot ping6 ${_addr}, rc=${_rc}"
		return 1
		;;
	esac
	return 0
}

reachability_check()
{
	local _i rc

	# Try to reach all control addresses on other nodes.
	# We need to loop for a while as we cannot expect all to be up
	# the very same moment.
	i=1
	rc=42
	while test ${rc} -ne 0 -a ${i} -le ${WAITS}; do
		print_debug "${i}/${WAITS} trying to ping6 control addresses."
		rc=0
		set +e
		case ${node} in
		left)	_reachability_check ${MIDDLELEFTADDR}
			rc=$((rc + $?))
			_reachability_check ${MIDDLERIGHTADDR}
			rc=$((rc + $?))
			_reachability_check ${RIGHTADDR}
			rc=$((rc + $?))
			;;
		middle)	_reachability_check ${LEFTADDR}
			rc=$((rc + $?))
			_reachability_check ${RIGHTADDR}
			rc=$((rc + $?))
			;;
		right)	_reachability_check ${MIDDLERIGHTADDR}
			rc=$((rc + $?))
			_reachability_check ${MIDDLELEFTADDR}
			rc=$((rc + $?))
			_reachability_check ${LEFTADDR}
			rc=$((rc + $?))
			;;
		esac
		set -e
		sleep 1
		i=$((i + 1))
	done
}

################################################################################
#
# "Greeting" handling to sync notes to the agreed upon next test case.
#
send_control_msg()
{
        local _case _addr i rc _msg _keyword _fibs
	_case="$1"
	_addr="$2"

	set +e
	i=0
	rc=-1
	while test ${i} -lt ${WAITS} -a ${rc} -ne 0; do
		print_debug "Sending control msg #${i} to peer ${_addr}"
		_msg=`echo "${_case} ${RT_NUMFIBS}" | \
		    nc -6 -w 1 ${_addr} ${CTRLPORT}`
		rc=$?
		i=$((i + 1))
		# Might sleep longer in total but better than to DoS
		# and not get anywhere.
		sleep 1
	done
	set -e

	read _keyword _fibs <<EOI
${_msg}
EOI
	print_debug "_keyword=${_keyword}"
	print_debug "_fibs=${_fibs}"
	case ${_keyword} in
	${_case});;
	*)	die "Got invalid keyword from ${_addr} in control message:" \
		    "${_msg}"
	;;
	esac
	if test ${_fibs} -ne ${RT_NUMFIBS}; then
		die "Number of FIBs not matching ours (${RT_NUMFIBS}) in" \
		    "control message from ${_addr}: ${_msg}"
	fi

	print_debug "Successfully exchanged control message with ${_addr}."
}

send_control_msgs()
{
	local _case _addr
	_case="$1"
	
	# Always start with the far end.  Otherwise we will cut that off when
	# cleanly taering down things again.
	for _addr in ${RIGHTADDR} ${MIDDLELEFTADDR}; do
		send_control_msg "${_case}" ${_addr}
	done

	# Allow us to flush ipfw counters etc before new packets will arrive.
	sleep 1
}

# We are setup.  Wait for the initiator to tell us that it is ready.
wait_remote_ready()
{
        local _case _msg _keyword _fibs
	_case="$1"

	# Wait for the remote to connect and start things.
	# We tell it the magic keyword, and our number of FIBs.
	_msg=`echo "${_case} ${RT_NUMFIBS}" | nc -6 -l ${CTRLPORT}`

	read _keyword _fibs <<EOI
${_msg}
EOI
	print_debug "_keyword=${_keyword}"
	print_debug "_fibs=${_fibs}"
	case ${_keyword} in
	${_case});;
	*)	die "Got invalid keyword in control message: ${_msg}"
		;;
	esac
	if test ${_fibs} -ne ${RT_NUMFIBS}; then
		die "Number of FIBs not matching ours (${RT_NUMFIBS}) in" \
		    "control message: ${_msg}"
	fi

	print_debug "Successfully received control message."
}

################################################################################
#
# Test case helper functions.
#
# Please note that neither on the intiator nor the reflector are FIBs despite
# a variable name might indicate.  If such a variable is used it mirrors FIB
# numbers from the middle node to match for test cases.
#
test_icmp6()
{
	local _maxfibs _addr _n _testno i _rc _ec
	_maxfibs=$1
	_addr="$2"
	_n="$3"

	printf "1..%d\n" ${_maxfibs}
	_testno=1
	set +e
	i=0
	while test ${i} -lt ${_maxfibs}; do
		_txt="${_n}_${i}"
		print_debug "Testing ${_txt}"

		# Generate HEX for ping6 payload.
		_fibtxt=`echo "${_txt}" | hd -v | cut -b11-60 | tr -d ' \r\n'`

		eval _rc="\${rc_${i}}"
		ping6 -n -c1 -p ${_fibtxt} ${_addr} > /dev/null 2>&1
		_ec=$?
		# We need to normalize the exit code of ping6.
		case ${_ec} in
		0)	;;
		*)	_ec=1 ;;
		esac
		check_rc ${_ec} ${_rc} ${_testno} "${_txt}" "FIB ${i} ${_addr}"
		testno=$((testno + 1))
		i=$((i + 1))
	done
	set -e
}

test_ulp_reflect_one()
{
	local _txt _opts port fib
	_txt="$1"
	_opts="$2"
	port=$3
	fib=$4

	print_debug "./reflect -p $((port + 1 + fib)) -t ${_txt}" "${_opts}"
	./reflect -p $((port + 1 + fib)) -t ${_txt} ${_opts}
	print_debug "reflect '${_txt}' terminated without error."
}

test_ulp_reflect_multiple()
{
	local _maxfibs _txt _opts i _jobs _p
	_maxfibs=$1
	_txt="$2"
	_opts="$3"

	i=0
	_jobs=""
	while test ${i} -lt ${_maxfibs}; do
		print_debug "./reflect -p $((CTRLPORT + 1000 + i))" \
		    "-t ${_txt} ${_opts} -N -f ${i} &"
		./reflect -p $((CTRLPORT + 1000 + i)) \
		    -t ${_txt} ${_opts} -N -f ${i} &
		_p=$!
		_jobs="${_jobs}${_p} "
		i=$((i + 1))
	done

	# Start OOB control connection for START/DONE.
	testrx_run_one "${_txt}" "${_opts}"
	print_debug "KILL ${_jobs}"
	for i in ${_jobs}; do
		kill ${i} || true
	done
	#killall reflect || true
	print_debug "reflects for '${_txt}' terminated without error."
}

nc_send_recv()
{
	local _loops _msg _expreply _addr _port _opts i
	_loops=$1
	_msg="$2"
	_expreply="$3"
	_addr=$4
	_port=$5
	_opts="$6"

	i=0
	while test ${i} -lt ${_loops}; do
		i=$((i + 1))
		print_debug "e ${_msg} | nc -6 -w1 ${_opts} ${_addr} ${_port}"
		_reply=`echo "${_msg}" | nc -6 -w1 ${_opts} ${_addr} ${_port}`
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

test_ulp()
{
	local maxfibs _msg _addr port fib i _txt testno _rc _reply
	maxfibs=$1
	_msg="$2"
	_addr=$3
	port=$4
	fib=$5

	printf "1..%d\n" $((${maxfibs} * 2))
	testno=1
	i=0
	while test ${i} -lt ${maxfibs}; do

		if test ${i} -eq $((${maxfibs} - 1)); then
			# Last one; signal DONE.
			_txt="DONE ${_msg}_${i}"
		else
			_txt="DONE ${_msg}_${i}"
		fi

		eval _rc="\${rc_${i}}"

		# Test TCP.
		nc_send_recv ${maxfibs} "${_txt}" "${_txt}" ${_addr} \
		    $((${port} + 1 + fib)) ""
		check_rc $? ${_rc} ${testno} "${_msg}_${i}_tcp" \
		    "[${_addr}]:$((${port} + 1 + fib)) ${_reply}"
		testno=$((testno + 1))
		sleep 1

		# Test UDP.
		nc_send_recv ${maxfibs} "${_txt}" "${_txt}" ${_addr} \
		    $((${port} + 1 + fib)) "-u"
		check_rc $? ${_rc} ${testno} "${_msg}_${i}_udp" \
		    "[${_addr}]:$((${port} + 1 + fib)) ${_reply}"
		sleep 1

		i=$((i + 1))
		testno=$((testno + 1))
	done
}

setup_ipfw_count()
{
	local i port maxfib _p _fib _ofib
	port=$1
	maxfib=$2
	_fib=$3
	_ofib=$4

	i=0
	while test ${i} -lt ${maxfib}; do

		case ${_ofib} in
		-1)	_p=$((port + 1 + i)) ;;
		*)	_p=$((port + 1 + maxfib - 1 - i)) ;;
		esac

		# Only count ICMP6 echo replies.
		ipfw add $((10000 + i)) count ipv6-icmp from any to any \
		    icmp6types 129 fib ${i} via ${IFACE} out > /dev/null
		ipfw add $((10000 + i)) count tcp from any to any \
		    src-port ${_p} fib ${i}  via ${IFACE} out > /dev/null
		ipfw add $((10000 + i)) count udp from any to any \
		    src-port ${_p} fib ${i} via ${IFACE} out > /dev/null

		# Only count ICMP6 echo requests.
		ipfw add $((20000 + i)) count ipv6-icmp from any to any \
		    icmp6types 128 fib ${i} via ${IFACEFAR} out > /dev/null
		ipfw add $((20000 + i)) count tcp from any to any \
		    dst-port $((${port} + 1 + i)) fib ${i} \
		    via ${IFACEFAR} out > /dev/null
		ipfw add $((20000 + i)) count udp from any to any \
		    dst-port $((${port} + 1 + i)) fib ${i} \
		    via ${IFACEFAR} out > /dev/null

		i=$((i + 1))
	done
}

report_ipfw_count()
{
	local _fib _o i _rstr _c _req _p _opts base
	_o="$2"

	case ${DEBUG} in
	''|0)	;;
	*)	ipfw show ;;
	esac

	_rstr="RESULTS "
	for base in 10000 20000; do
		for _o in i t u; do
			case ${base} in
			10000)	_rstr="${_rstr}\nLEFT " ;;
			20000)	_rstr="${_rstr}\nRIGHT " ;;
			esac
			case ${_o} in
			i)	_rstr="${_rstr}ICMP6 " ;;
			t)	_rstr="${_rstr}TCP " ;;
			u)	_rstr="${_rstr}UDP " ;;
			esac
			i=0
			while test ${i} -lt ${RT_NUMFIBS}; do

				case "${_o}" in
				i)	_c=`ipfw show $((${base} + i)) | \
					    awk '/ ipv6-icmp / { print $2 }'` ;;
				t)	_c=`ipfw show $((${base} + i)) | \
					    awk '/ tcp / { print $2 }'` ;;
				u)	_c=`ipfw show $((${base} + i)) | \
					    awk '/ udp / { print $2 }'` ;;
				esac
				_rstr="${_rstr}${i} ${_c},"

				i=$((i + 1))
			done
		done
		i=0
		while test ${i} -lt ${RT_NUMFIBS}; do
			ipfw delete $((${base} + i)) > /dev/null 2>&1 || true
			i=$((i + 1))
		done
	done

	# We do not care about the request.
	_req=`printf "${_rstr}" | nc -6 -l $((${CTRLPORT} - 1))`
	print_debug "$? -- ${_req} -- ${_rstr}"
}

fetch_ipfw_count()
{
	local _n _reply _line _edge _type _fib _count _rc _ec _status
	_n="$1"

	# Leave node some time to build result set.
	sleep 3

	print_debug "Asking for ipfw count results..."
	set +e
	nc_send_recv 1 "RESULT REQUEST" "" ${MIDDLELEFTADDR} \
	    $((${CTRLPORT} - 1)) ""
	set -e
	case "${_reply}" in
	RESULTS\ *)	;;
	*)		die "Got invalid reply from peer." \
			    "Expected 'RESULTS ...', got '${_reply}'" ;;
	esac

	# Trim "RESULTS "
	_reply=${_reply#* }

	# FIBs * {left, right} * {icmp6, tcp, udp}
	printf "1..%d\n" $((RT_NUMFIBS * 2 * 3))
	testno=1
	while read _line; do
		print_debug "_line == ${_line}"
		_edge=${_line%% *}
		_line=${_line#* }
		_type=${_line%% *}
		_line=${_line#* }

		while read _fib _count; do
			eval _em="\${rc_${_n}_${_edge}_${_type}_${_fib}}"
			: ${_em:=-42}
			if test ${_count} -gt 0; then
				_rc=1
			else
				_rc=0
			fi
			if test ${_rc} -eq ${_em}; then
				_status="ok"
			else
				_status="not ok"
			fi
			printf "%s %d %s # count=%s _rc=%d _em=%d\n" \
			    "${_status}" ${testno} "${_n}_${_edge}_${_type}_${_fib}" \
			    ${_count} ${_rc} ${_em}
			testno=$((testno + 1))
		done <<EOi
`printf "${_line}" | tr ',' '\n'`
EOi
		
	done <<EOo
`printf "${_reply}" | grep -v "^$"`
EOo

	print_debug "ipfw count results processed"
}

################################################################################
#
# Test cases.
#
# In general we set the FIB on in, but count on out.
#

_fwd_default_fib_symmetric_results()
{
	local _n i _edge _type _rc
	_n="$1"

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		for _edge in "LEFT" "RIGHT"; do
			for _type in "ICMP6" "TCP" "UDP"; do

				case ${i} in
				0)	eval rc_${_n}_${_edge}_${_type}_${i}=1
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=1"
					;;
				*)	eval rc_${_n}_${_edge}_${_type}_${i}=0
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=0"
					;;
				esac

			done
		done
		i=$((i + 1))
	done
}

_fwd_default_fib_symmetric_left()
{
	local _n
	_n="$1"

	send_control_msgs "START_${_n}"

	# Setup expected return code
	rc_0=0

	# Initiate probes for ICMP6, TCP and UDP.
	test_icmp6 1 ${RIGHTADDR} "${_n}_icmp6"
	test_ulp 1 "${_n}" ${RIGHTADDR} ${CTRLPORT} 0

	send_control_msgs "STOP_${_n}"
	_fwd_default_fib_symmetric_results "${_n}"
	fetch_ipfw_count "${_n}"
}

_fwd_default_fib_symmetric_middle()
{
	local _n
	_n="$1"

	setup_ipfw_count ${CTRLPORT} ${RT_NUMFIBS} 0 -1
	wait_remote_ready "START_${_n}"
	ipfw -q zero > /dev/null
	# Nothing to do for the middle node testing the default.
	sleep 1
	wait_remote_ready "STOP_${_n}"
	report_ipfw_count
}

_fwd_default_fib_symmetric_right()
{
	local _n
	_n="$1"

	wait_remote_ready "START_${_n}"

	# No need to do anything for ICMPv6.
	# Start reflect for TCP and UDP.
	test_ulp_reflect_one "${_n}_tcp" "-N -T TCP6" 0 ${CTRLPORT}
	test_ulp_reflect_one "${_n}_udp" "-N -T UDP6" 0 ${CTRLPORT}

	wait_remote_ready "STOP_${_n}"
}

fwd_default_fib_symmetric()
{
	local _n

	_n="fwd_default_fib_symmetric"

	print_debug "${_n}"
	case ${node} in
	left)	_fwd_default_fib_symmetric_left ${_n} ;;
	middle)	_fwd_default_fib_symmetric_middle ${_n} ;;
	right)	_fwd_default_fib_symmetric_right ${_n} ;;
	esac
}

_fwd_default_fib_symmetric_middle_ifconfig()
{
	local _n
	_n="$1"

	ifconfig ${IFACE} fib 0
	ifconfig ${IFACEFAR} fib 0
	setup_ipfw_count ${CTRLPORT} ${RT_NUMFIBS} 0 -1
	wait_remote_ready "START_${_n}"
	ipfw -q zero > /dev/null
	# Nothing to do for the middle node testing the default.
	sleep 1
	wait_remote_ready "STOP_${_n}"
	report_ipfw_count
}

fwd_default_fib_symmetric_ifconfig()
{
	local _n

	_n="fwd_default_fib_symmetric_ifconfig"

	print_debug "${_n}"
	case ${node} in
	left)	_fwd_default_fib_symmetric_left ${_n} ;;
	middle)	_fwd_default_fib_symmetric_middle_ifconfig ${_n} ;;
	right)	_fwd_default_fib_symmetric_right ${_n} ;;
	esac
}

_fwd_default_fib_symmetric_middle_ipfw()
{
	local _n
	_n="$1"

	ipfw add 100 setfib 0 ipv6-icmp from any to any \
	    icmp6types 128 via ${IFACE} in > /dev/null
	ipfw add 100 setfib 0 ip6 from any to any \
	    proto tcp dst-port ${CTRLPORT} via ${IFACE} in > /dev/null
	ipfw add 100 setfib 0 ip6 from any to any \
	    proto udp dst-port ${CTRLPORT} via ${IFACE} in > /dev/null

	ipfw add 100 setfib 0 ipv6-icmp from any to any \
	    icmp6types 128 via ${IFACEFAR} in > /dev/null
	ipfw add 100 setfib 0 tcp from any to any \
	    dst-port ${CTRLPORT} via ${IFACEFAR} in > /dev/null
	ipfw add 100 setfib 0 udp from any to any \
	    dst-port ${CTRLPORT} via ${IFACEFAR} in > /dev/null

	setup_ipfw_count ${CTRLPORT} ${RT_NUMFIBS} 0 -1
	wait_remote_ready "START_${_n}"
	ipfw -q zero > /dev/null
	# Nothing to do for the middle node testing the default.
	sleep 1
	wait_remote_ready "STOP_${_n}"
	report_ipfw_count

	ipfw delete 100 > /dev/null
}

fwd_default_fib_symmetric_ipfw()
{
	local _n

	_n="fwd_default_fib_symmetric_ipfw"

	print_debug "${_n}"
	case ${node} in
	left)	_fwd_default_fib_symmetric_left ${_n} ;;
	middle)	_fwd_default_fib_symmetric_middle_ipfw ${_n} ;;
	right)	_fwd_default_fib_symmetric_right ${_n} ;;
	esac
}

################################################################################

_fwd_fib_symmetric_results()
{
	local _n _fib i _edge _type _rc
	_n="$1"
	_fib=$2

	i=0
	while test ${i} -lt ${RT_NUMFIBS}; do
		for _edge in "LEFT" "RIGHT"; do
			for _type in "ICMP6" "TCP" "UDP"; do

				case ${i} in
				${_fib}) eval rc_${_n}_${_edge}_${_type}_${i}=1
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=1"
					;;
				*)	eval rc_${_n}_${_edge}_${_type}_${i}=0
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=0"
					;;
				esac

			done
		done
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_left()
{
	local _n _maxfib i
	_n="$1"
	_maxfib=$2

	# Setup expected return code
	i=0
	while test ${i} -lt ${_maxfib}; do
		eval rc_${i}=0
		i=$((i + 1))
	done

	# Initiate probes for ICMP6, TCP and UDP.
	i=0
	while test ${i} -lt ${_maxfib}; do

		sleep 1

		send_control_msgs "START_${_n}_${i}"

		test_icmp6 1 ${RIGHTADDR} "${_n}_${i}_icmp6"
		test_ulp 1 "${_n}_${i}" ${RIGHTADDR} ${CTRLPORT} ${i}

		send_control_msgs "STOP_${_n}_${i}"
		_fwd_fib_symmetric_results "${_n}_${i}" ${i}
		fetch_ipfw_count "${_n}_${i}"
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_right()
{
	local _n _maxfib i
	_n="$1"
	_maxfib=$2

	i=0
	while test ${i} -lt ${_maxfib}; do
		wait_remote_ready "START_${_n}_${i}"

		# No need to do anything for ICMPv6.
		# Start reflect for TCP and UDP.
		test_ulp_reflect_one "${_n}_tcp" "-N -T TCP6" ${i} ${CTRLPORT}
		test_ulp_reflect_one "${_n}_udp" "-N -T UDP6" ${i} ${CTRLPORT}

		wait_remote_ready "STOP_${_n}_${i}"
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_middle_ifconfig()
{
	local _n _maxfib i
	_n="$1"
	_maxfib=$2

	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACE} fib ${i}
		ifconfig ${IFACEFAR} fib ${i}
		setup_ipfw_count ${CTRLPORT} ${_maxfib} ${i} -1
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_middle_ipfw()
{
	local _n _maxfib i _port
	_n="$1"
	_maxfib=$2

	i=0
	while test ${i} -lt ${_maxfib}; do
		_port=$((CTRLPORT + 1 + i))
		ipfw add 100 setfib ${i} ipv6-icmp from any to any \
		    icmp6types 128 via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} tcp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} udp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null

		ipfw add 100 setfib ${i} ipv6-icmp from any to any \
		    icmp6types 129 via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${i} tcp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${i} udp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null

		setup_ipfw_count ${CTRLPORT} ${_maxfib} ${i} -1
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count

		ipfw delete 100 > /dev/null
		i=$((i + 1))
	done
}

fwd_fib_symmetric_ifconfig()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_symmetric_ifconfig"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_symmetric_left ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_symmetric_middle_ifconfig ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_right ${_n} ${_maxfib} ;;
	esac
}

fwd_fib_symmetric_ipfw()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_symmetric_ipfw"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_symmetric_left ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_symmetric_middle_ipfw ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_right ${_n} ${_maxfib} ;;
	esac
}

################################################################################

_fwd_fib_asymmetric_results()
{
	local _n fib maxfib i _edge _type _rc
	_n="$1"
	fib=$2
	maxfib=$3

	i=0
	while test ${i} -lt ${maxfib}; do
		_edge="RIGHT"
			for _type in "ICMP6" "TCP" "UDP"; do

				case ${i} in
				${fib}) eval rc_${_n}_${_edge}_${_type}_${i}=1
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=1"
					;;
				*)	eval rc_${_n}_${_edge}_${_type}_${i}=0
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=0"
					;;
				esac

			done
		i=$((i + 1))
	done
	fib=$((maxfib - 1 - fib))
	i=0
	while test ${i} -lt ${maxfib}; do
		_edge="LEFT"
			for _type in "ICMP6" "TCP" "UDP"; do

				case ${i} in
				${fib}) eval rc_${_n}_${_edge}_${_type}_${i}=1
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=1"
					;;
				*)	eval rc_${_n}_${_edge}_${_type}_${i}=0
					#print_debug \
					#   "rc_${_n}_${_edge}_${_type}_${i}=0"
					;;
				esac

			done
		i=$((i + 1))
	done
}

_fwd_fib_asymmetric_left()
{
	local _n _maxfib i
	_n="$1"
	_maxfib=$2

	# Setup expected return code
	i=0
	while test ${i} -lt ${_maxfib}; do
		eval rc_${i}=0
		i=$((i + 1))
	done

	# Initiate probes for ICMP6, TCP and UDP.
	i=0
	while test ${i} -lt ${_maxfib}; do

		sleep 1

		send_control_msgs "START_${_n}_${i}"

		test_icmp6 1 ${RIGHTADDR} "${_n}_${i}_icmp6"
		test_ulp 1 "${_n}_${i}" ${RIGHTADDR} ${CTRLPORT} ${i}

		send_control_msgs "STOP_${_n}_${i}"
		_fwd_fib_asymmetric_results "${_n}_${i}" ${i} ${_maxfib}
		fetch_ipfw_count "${_n}_${i}"
		i=$((i + 1))
	done
}

_fwd_fib_asymmetric_middle_ifconfig()
{
	local _n maxfib i
	_n="$1"
	maxfib=$2

	i=0
	while test ${i} -lt ${maxfib}; do
		ifconfig ${IFACE} fib ${i}
		ifconfig ${IFACEFAR} fib $((${maxfib} - 1 - ${i}))
		setup_ipfw_count ${CTRLPORT} ${maxfib} ${i} \
		    $((${maxfib} - 1 - ${i}))
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count
		i=$((i + 1))
	done
}

_fwd_fib_asymmetric_middle_ipfw()
{
	local _n maxfib i j _port
	_n="$1"
	maxfib=$2

	i=0
	while test ${i} -lt ${maxfib}; do

		_port=$((CTRLPORT + 1 + i))
		ipfw add 100 setfib ${i} ipv6-icmp from any to any \
		    icmp6types 128 via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} tcp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} udp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null

		j=$((${maxfib} - 1 - ${i}))
		ipfw add 100 setfib ${j} ipv6-icmp from any to any \
		    icmp6types 129 via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${j} tcp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${j} udp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null

		setup_ipfw_count ${CTRLPORT} ${maxfib} ${i} ${j}
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count

		ipfw delete 100 > /dev/null
		i=$((i + 1))
	done
}

fwd_fib_asymmetric_ifconfig()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_asymmetric_ifconfig"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_asymmetric_left ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_asymmetric_middle_ifconfig ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_right ${_n} ${_maxfib} ;;
	esac
}

fwd_fib_asymmetric_ipfw()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_asymmetric_ipfw"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_asymmetric_left ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_asymmetric_middle_ipfw ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_right ${_n} ${_maxfib} ;;
	esac
}

################################################################################

_fwd_fib_symmetric_destructive_left()
{
	local _n _maxfib i _addr
	_n="$1"
	_maxfib=$2

	# Setup expected return code
	i=0
	while test ${i} -lt ${_maxfib}; do
		eval rc_${i}=0
		i=$((i + 1))
	done

	# Add default route.
	route add -net -inet6 default ${MIDDLELEFTADDR} > /dev/null

	# Initiate probes for ICMP6, TCP and UDP.
	i=0
	while test ${i} -lt ${_maxfib}; do

		sleep 1

		send_control_msgs "START_${_n}_${i}"

		_addr="2001:2:${i}::2"
		test_icmp6 1 ${_addr} "${_n}_${i}_icmp6"
		test_ulp 1 "${_n}_${i}" ${_addr} ${CTRLPORT} ${i}

		send_control_msgs "STOP_${_n}_${i}"
		_fwd_fib_symmetric_results "${_n}_${i}" ${i}
		fetch_ipfw_count "${_n}_${i}"
		i=$((i + 1))
	done

	# Cleanup networking.
	route delete -net -inet6 default > /dev/null
}

_fwd_fib_symmetric_destructive_right()
{
	local _n _maxfib i _addr
	_n="$1"
	_maxfib=$2

	# Setup networking (ideally we'd use the link-local).
	route add -net -inet6 default ${MIDDLERIGHTADDR} > /dev/null 2>&1
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 alias
		i=$((i + 1))
	done

	i=0
	while test ${i} -lt ${_maxfib}; do
		wait_remote_ready "START_${_n}_${i}"

		# No need to do anything for ICMPv6.
		# Start reflect for TCP and UDP.
		_addr="2001:2:${i}::2"
		test_ulp_reflect_one "${_n}_tcp" "-N -T TCP6 -A ${_addr}" \
		    ${i} ${CTRLPORT}
		test_ulp_reflect_one "${_n}_udp" "-N -T UDP6 -A ${_addr}" \
		    ${i} ${CTRLPORT}

		wait_remote_ready "STOP_${_n}_${i}"
		i=$((i + 1))
	done

	# Cleanup networking again.
	route delete -net -inet6 default > /dev/null 2>&1
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 -alias
		i=$((i + 1))
	done

}


_fwd_fib_symmetric_destructive_middle_setup_networking()
{
	local _maxfib i j
	_maxfib=$1

	# Setup networking.
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACEFAR} inet6 2001:2:${i}::1/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACEFAR} inet6 2001:2:${i}::1/64 alias
		j=0
		while test ${j} -lt ${_maxfib}; do
			# Only work on all other FIBs.
			if test ${j} -ne ${i}; then
				setfib -F ${j} route delete -net -inet6 \
				     2001:2:${i}::/64 > /dev/null
			fi
			j=$((j + 1))
		done
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_destructive_middle_cleanup_networking()
{
	local _maxfib i
	_maxfib=$1

	# Cleanup networking again.
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACEFAR} inet6 2001:2:${i}::1/64 -alias
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_destructive_middle_ifconfig()
{
	local _n _maxfib i
	_n="$1"
	_maxfib=$2

	_fwd_fib_symmetric_destructive_middle_setup_networking ${_maxfib}

	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACE} fib ${i}
		ifconfig ${IFACEFAR} fib ${i}
		setup_ipfw_count ${CTRLPORT} ${_maxfib} ${i} -1
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count
		i=$((i + 1))
	done

	_fwd_fib_symmetric_destructive_middle_cleanup_networking ${_maxfib}
}

_fwd_fib_symmetric_destructive_middle_ipfw()
{
	local _n _maxfib i _port
	_n="$1"
	_maxfib=$2

	_fwd_fib_symmetric_destructive_middle_setup_networking ${_maxfib}

	i=0
	while test ${i} -lt ${_maxfib}; do
		_port=$((CTRLPORT + 1 + i))
		ipfw add 100 setfib ${i} ipv6-icmp from any to any \
		    icmp6types 128 via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} tcp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} udp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null

		ipfw add 100 setfib ${i} ipv6-icmp from any to any \
		    icmp6types 129 via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${i} tcp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${i} udp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null

		setup_ipfw_count ${CTRLPORT} ${_maxfib} ${i} -1
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count

		ipfw delete 100 > /dev/null
		i=$((i + 1))
	done

	_fwd_fib_symmetric_destructive_middle_cleanup_networking ${_maxfib}
}

fwd_fib_symmetric_destructive_ifconfig()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_symmetric_destructive_ifconfig"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_symmetric_destructive_left ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_symmetric_destructive_middle_ifconfig \
		    ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_destructive_right ${_n} ${_maxfib} ;;
	esac
}

fwd_fib_symmetric_destructive_ipfw()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_symmetric_destructive_ipfw"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_symmetric_destructive_left ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_symmetric_destructive_middle_ipfw \
		    ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_destructive_right ${_n} ${_maxfib} ;;
	esac
}

################################################################################

_fwd_fib_symmetric_destructive_defroute_left()
{
	local _n _maxfib i _addr
	_n="$1"
	_maxfib=$2

	# Setup expected return code
	i=0
	while test ${i} -lt ${_maxfib}; do
		eval rc_${i}=0
		i=$((i + 1))
	done

	# Add default route.
	route delete -net -inet6 default > /dev/null 2>&1 || true
	route add -net -inet6 default ${MIDDLELEFTADDR} > /dev/null

	# Initiate probes for ICMP6, TCP and UDP.
	_addr="2001:2:1234::2"
	i=0
	while test ${i} -lt ${_maxfib}; do

		sleep 1

		send_control_msgs "START_${_n}_${i}"

		test_icmp6 1 "${_addr}" "${_n}_${i}_icmp6"
		test_ulp 1 "${_n}_${i}" "${_addr}" ${CTRLPORT} ${i}

		send_control_msgs "STOP_${_n}_${i}"
		_fwd_fib_symmetric_results "${_n}_${i}" ${i}
		fetch_ipfw_count "${_n}_${i}"
		i=$((i + 1))
	done

	# Cleanup networking.
	route delete -net -inet6 default > /dev/null 2>&1
}

_fwd_fib_symmetric_destructive_defroute_right()
{
	local _n _maxfib i _addr
	_n="$1"
	_maxfib=$2

	# Setup networking (ideally we'd use the link-local).
	route delete -net -inet6 default > /dev/null 2>&1 ||  true
	route add -net -inet6 default ${MIDDLERIGHTADDR} > /dev/null 2>&1
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 alias
		i=$((i + 1))
	done
	_addr="2001:2:1234::2"
	ifconfig lo0 inet6 ${_addr}/128 alias

	i=0
	while test ${i} -lt ${_maxfib}; do
		wait_remote_ready "START_${_n}_${i}"

		# No need to do anything for ICMPv6.
		# Start reflect for TCP and UDP.
		test_ulp_reflect_one "${_n}_tcp" "-N -T TCP6 -A ${_addr}" \
		    ${i} ${CTRLPORT}
		test_ulp_reflect_one "${_n}_udp" "-N -T UDP6 -A ${_addr}" \
		    ${i} ${CTRLPORT}

		wait_remote_ready "STOP_${_n}_${i}"
		i=$((i + 1))
	done

	# Cleanup networking again.
	route delete -net -inet6 default > /dev/null 2>&1
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACE} inet6 2001:2:${i}::2/64 -alias
		i=$((i + 1))
	done
	ifconfig lo0 inet6 ${_addr}/128 -alias

}

_fwd_fib_symmetric_destructive_defroute_middle_setup_networking()
{
	local _maxfib i j
	_maxfib=$1

	# Setup networking.
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACEFAR} inet6 2001:2:${i}::1/64 -alias \
		    > /dev/null 2>&1 || true
		ifconfig ${IFACEFAR} inet6 2001:2:${i}::1/64 alias
		j=0
		while test ${j} -lt ${_maxfib}; do
			# Only work on all other FIBs.
			if test ${j} -ne ${i}; then
				setfib -F ${j} route delete -net -inet6 \
				     2001:2:${i}::/64 > /dev/null
			fi
			j=$((j + 1))
		done
		setfib -F ${i} route delete -net -inet6 \
		     2001:2:1234::2 2001:2:${i}::2 > /dev/null 2>&1 || true
		setfib -F ${i} route add -net -inet6 \
		     2001:2:1234::2 2001:2:${i}::2 > /dev/null
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_destructive_defroute_middle_cleanup_networking()
{
	local _maxfib i
	_maxfib=$1

	# Cleanup networking again.
	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACEFAR} inet6 2001:2:${i}::1/64 -alias
		setfib -F ${i} route delete -net -inet6 \
		     2001:2:1234::2 2001:2:${i}::2 > /dev/null
		i=$((i + 1))
	done
}

_fwd_fib_symmetric_destructive_defroute_middle_ifconfig()
{
	local _n _maxfib i
	_n="$1"
	_maxfib=$2

	_fwd_fib_symmetric_destructive_defroute_middle_setup_networking \
	     ${_maxfib}

	i=0
	while test ${i} -lt ${_maxfib}; do
		ifconfig ${IFACE} fib ${i}
		ifconfig ${IFACEFAR} fib ${i}
		setup_ipfw_count ${CTRLPORT} ${_maxfib} ${i} -1
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count
		i=$((i + 1))
	done

	_fwd_fib_symmetric_destructive_defroute_middle_cleanup_networking \
	    ${_maxfib}
}

_fwd_fib_symmetric_destructive_defroute_middle_ipfw()
{
	local _n _maxfib i _port
	_n="$1"
	_maxfib=$2

	_fwd_fib_symmetric_destructive_defroute_middle_setup_networking \
	    ${_maxfib}

	i=0
	while test ${i} -lt ${_maxfib}; do
		_port=$((CTRLPORT + 1 + i))
		ipfw add 100 setfib ${i} ipv6-icmp from any to any \
		    icmp6types 128 via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} tcp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null
		ipfw add 100 setfib ${i} udp from any to any \
		    dst-port ${_port} via ${IFACE} in > /dev/null

		ipfw add 100 setfib ${i} ipv6-icmp from any to any \
		    icmp6types 129 via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${i} tcp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null
		ipfw add 100 setfib ${i} udp from any to any \
		    src-port ${_port} via ${IFACEFAR} in > /dev/null

		setup_ipfw_count ${CTRLPORT} ${_maxfib} ${i} -1
		wait_remote_ready "START_${_n}_${i}"
		ipfw -q zero > /dev/null
		# Nothing to do for the middle node testing the default.
		sleep 1
		wait_remote_ready "STOP_${_n}_${i}"
		report_ipfw_count

		ipfw delete 100 > /dev/null
		i=$((i + 1))
	done

	_fwd_fib_symmetric_destructive_defroute_middle_cleanup_networking \
	    ${_maxfib}
}

fwd_fib_symmetric_destructive_defroute_ifconfig()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_symmetric_destructive_defroute_ifconfig"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_symmetric_destructive_defroute_left \
		    ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_symmetric_destructive_defroute_middle_ifconfig \
		    ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_destructive_defroute_right \
		    ${_n} ${_maxfib} ;;
	esac
}

fwd_fib_symmetric_destructive_defroute_ipfw()
{
	local _maxfib _n
	_maxfib=$1

	_n="fwd_fib_symmetric_destructive_defroute_ipfw"

	print_debug "${_n} ${_maxfib}"
	case ${node} in
	left)	_fwd_fib_symmetric_destructive_defroute_left \
		    ${_n} ${_maxfib} ;;
	middle)	_fwd_fib_symmetric_destructive_defroute_middle_ipfw \
		    ${_n} ${_maxfib} ;;
	right)	_fwd_fib_symmetric_destructive_defroute_right \
		    ${_n} ${_maxfib} ;;
	esac
}

################################################################################
#
# MAIN
#

# Same for all hosts.
if test `sysctl -n security.jail.jailed` -eq 0; then
	kldload ipfw > /dev/null 2>&1 || kldstat -v | grep -q ipfw 
fi
ipfw -f flush > /dev/null 2>&1 || die "please load ipfw in base system"
ipfw add 65000 permit ip from any to any > /dev/null 2>&1

# Per host setup.
setup_networking
reachability_check

#
# Tests
#

fwd_default_fib_symmetric
fwd_default_fib_symmetric_ifconfig
fwd_default_fib_symmetric_ipfw

fwd_fib_symmetric_ifconfig ${RT_NUMFIBS}
fwd_fib_symmetric_ipfw ${RT_NUMFIBS}

fwd_fib_asymmetric_ifconfig ${RT_NUMFIBS}
fwd_fib_asymmetric_ipfw ${RT_NUMFIBS}

fwd_fib_symmetric_destructive_ifconfig ${RT_NUMFIBS}
fwd_fib_symmetric_destructive_ipfw ${RT_NUMFIBS}

fwd_fib_symmetric_destructive_defroute_ifconfig ${RT_NUMFIBS}
fwd_fib_symmetric_destructive_defroute_ipfw ${RT_NUMFIBS}

# Per host cleanup.
cleanup_networking

# end
